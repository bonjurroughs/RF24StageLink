/*
  RF24StageLink.cpp - implementation. See RF24StageLink.h for the overview.
  Copyright (C) 2026 bonjurroughs.  MIT License (see LICENSE).
*/
#include "RF24StageLink.h"

namespace {
  // Default hop set, spread for frequency diversity. ALL channels stay inside the
  // 2.4 GHz ISM band (nRF channel k => 2400+k MHz; ISM is ~ch 0-83 / 2400-2483 MHz).
  // Channels above ~83 are out-of-band: most nRF24 modules' antenna/matching is tuned
  // for ISM and they barely work up there - keep the whole set <= ~80.
  const uint8_t DEFAULT_CHANNELS[] = { 16, 22, 28, 34, 40 };
  const uint8_t DEFAULT_COUNT = (uint8_t)(sizeof(DEFAULT_CHANNELS) / sizeof(DEFAULT_CHANNELS[0]));
  const uint8_t DEFAULT_ADDR[5] = { 0xC1, 0x5A, 0x9E, 0x10, 0x01 };

  // Beat-sync tempo lock: the measurement window grows beat-by-beat from the acquisition
  // start, then FREEZES at this many beats. Growing the window is what makes the first lock
  // fast (a usable estimate at 2 beats); freezing keeps the locked tempo rock-steady. A new
  // song re-arms acquisition via MIDI Start. ~12 beats gives a final tightness of ~+/-1 BPM.
  const uint8_t TEMPO_LOCK_BEATS = 12;
}

RF24StageLink::RF24StageLink(RF24& radio)
  : _radio(radio), _role(ROLE_NONE),
    _count(0), _hopPos(0),
    _dataRate(RF24_250KBPS), _paLevel(RF24_PA_MAX), _hopMs(50), _redundancy(2),
    _reliable(false),
    _lastHopMs(0), _synced(false), _nextHopMs(0), _lastRxMs(0),
    _scanPos(0), _scanStartMs(0), _lastSeenHopPos(0xFF),
    _midiSeq(0), _lastMidiSeq(0), _haveMidiSeq(false),
    _cbMidi(0), _cbNoteOn(0), _cbNoteOff(0), _cbControl(0), _cbProgram(0), _cbPitch(0),
    _cbClock(0), _cbStart(0), _cbStop(0), _cbContinue(0),
    _chanMask(0xFFFF), _dropActiveSensing(true), _dropMtcQuarterFrame(false),
    _absorbClock(false), _bpm88(0), _haveClock(false),
    _ppqnCount(0), _beatAnchorMs(0), _tempoStartMs(0), _acqBeats(0), _playing(false),
    _cbSync(0), _rxBpm88(0), _rxPhase15(0), _rxPlaying(false),
    _dbg(0), _wasSynced(false), _rxCount(0), _pktCount(0), _txCount(0), _dbgLastMs(0)
{
  setChannels(DEFAULT_CHANNELS, DEFAULT_COUNT);
  for (uint8_t i = 0; i < 5; ++i) _addr[i] = DEFAULT_ADDR[i];
}

void RF24StageLink::setChannels(const uint8_t* channels, uint8_t count) {
  if (count == 0) return;
  if (count > MAX_CHANNELS) count = MAX_CHANNELS;
  _count = count;
  for (uint8_t i = 0; i < count; ++i) _channels[i] = channels[i] > 125 ? 125 : channels[i];
  _hopPos = 0;
}

void RF24StageLink::setRadioAddress(const uint8_t* addr5) {
  for (uint8_t i = 0; i < 5; ++i) _addr[i] = addr5[i];
}

void RF24StageLink::applyRadioConfig() {
  _radio.setDataRate(_dataRate);
  _radio.setPALevel(_paLevel);
  _radio.enableDynamicPayloads();    // variable-length packets (beacon vs midi)
  _radio.enableDynamicAck();         // per-write NO_ACK control (beacons are always NO_ACK)
  _radio.setCRCLength(RF24_CRC_16);  // catch corruption
  if (_reliable) {
    // Point-to-point: the single fixture ACKs each MIDI write; hardware retransmits
    // until ACK or retries exhausted. ~750us x up to 5 tries fits inside the dwell.
    _radio.setAutoAck(true);
    _radio.setRetries(2, 5);
  } else {
    // Broadcast: no per-fixture ACK, no auto-retransmit (we cannot retransmit to many).
    _radio.setAutoAck(false);
    _radio.setRetries(0, 0);
  }
}

void RF24StageLink::hopTo(uint8_t pos) {
  _hopPos = pos;
  if (_role == ROLE_FIXTURE) {
    // Many nRF24 modules will NOT retune via setChannel() while listening (CE high).
    // Bracket with stop/startListening so the radio actually moves to the new channel.
    _radio.stopListening();
    _radio.setChannel(_channels[pos]);
    _radio.startListening();
  } else {
    _radio.setChannel(_channels[pos]); // master is a transmitter (standby); plain retune is fine
  }
}

bool RF24StageLink::txCurrent(const uint8_t* buf, uint8_t len, bool noAck) {
  // noAck=true -> multicast write, never waits for an ACK (beacons; broadcast MIDI).
  // noAck=false -> request an ACK; write() blocks until ACK or retries exhausted
  //                and returns whether it was delivered (reliable-mode MIDI).
  return _radio.write(buf, len, noAck);
}

// ===========================================================================
// Roles
// ===========================================================================
bool RF24StageLink::beginMaster() {
  _radio.begin();
  _role = ROLE_MASTER;
  applyRadioConfig();
  _radio.openWritingPipe(_addr);
  _radio.stopListening();
  hopTo(0);
  _lastHopMs = millis();
  return true;
}

bool RF24StageLink::beginFixture() {
  _radio.begin();
  _role = ROLE_FIXTURE;
  applyRadioConfig();
  _radio.openReadingPipe(1, _addr);
  _radio.startListening();
  hopTo(0);                 // role is FIXTURE, so this retunes via stop/startListening
  _synced      = false;
  _scanPos     = 0;
  _scanStartMs = millis();
  _lastRxMs    = millis();
  return true;
}

// ===========================================================================
// Pump
// ===========================================================================
void RF24StageLink::update() {
  uint32_t now = millis();

  if (_role == ROLE_MASTER) {
    if ((uint32_t)(now - _lastHopMs) >= _hopMs) {
      _lastHopMs += _hopMs;
      if ((uint32_t)(now - _lastHopMs) > (uint32_t)_hopMs * 4) _lastHopMs = now; // resync after a stall
      hopTo((uint8_t)((_hopPos + 1) % _count));
      // Compute the current beat phase the SAME way FastLED's beat88() does, so a
      // fixture can reproduce it exactly: phase16 = ((ms_since_beat) * bpm88 * 280) >> 16.
      // We ship the top 15 bits (the dropped LSB is ~1/65536 of a beat - imperceptible).
      uint16_t phase15 = 0;
      if (_bpm88) {
        uint32_t dt = now - _beatAnchorMs;
        uint16_t phase16 = (uint16_t)(((uint64_t)dt * _bpm88 * 280ULL) >> 16);
        phase15 = phase16 >> 1;
      }
      uint8_t buf[sl::MAX_PACKET];
      uint8_t n = sl::encodeBeacon(buf, _hopPos, _bpm88, phase15, _playing);
      // Beacon is sync-critical, so send it redundantly (>=2) - this is what lets a
      // fixture hold lock on a lossy link. Tiny airtime (6 bytes).
      uint8_t reps = _redundancy < 2 ? 2 : _redundancy;
      for (uint8_t i = 0; i < reps; ++i) txCurrent(buf, n, /*noAck=*/true);  // beacons never ACK
    }
  } else if (_role == ROLE_FIXTURE) {
    // Hop slightly EARLY so we're already on the next channel before the master
    // beacons there (covers retune + poll latency). The wide dwell absorbs the rest.
    uint16_t guard = _hopMs / 10;             // ~5 ms at the default 50 ms dwell
    if (guard < 3) guard = 3;
    if (guard > 12) guard = 12;
    if (guard >= _hopMs) guard = _hopMs / 2;

    // Drain received packets.
    uint8_t pipe;
    while (_radio.available(&pipe)) {
      uint8_t len = _radio.getDynamicPayloadSize();
      if (len == 0 || len > sl::MAX_PACKET) break; // RF24 already flushes oversized payloads
      uint8_t buf[sl::MAX_PACKET];
      _radio.read(buf, len);
      sl::Header h;
      if (!sl::readHeader(buf, len, h)) continue;

      _lastRxMs = now;
      _pktCount++;   // diagnostics: ANY packet heard (beacon or MIDI)

      // Re-phase on the FIRST packet of each new dwell (the master's hopPos changed),
      // using ANY packet type - beacon OR MIDI. The dense MIDI stream then KEEPS us
      // locked instead of starving sync, and re-acquisition works even when beacons
      // are scarce/lossy. (The master sends the beacon first each dwell, so when we're
      // already aligned the reference is usually the beacon; under load the first MIDI
      // packet stands in.) Any small phase error self-corrects on the next dwell.
      if (h.hopPos != _lastSeenHopPos) {
        _lastSeenHopPos = h.hopPos;
        if (h.hopPos < _count) _hopPos = h.hopPos;  // align channel index to the master
        _nextHopMs = now + _hopMs - guard;          // lead the next hop slightly
        _synced    = true;
      }

      if (h.type == sl::T_MIDI && len >= 6) {
        // Dedup: the master sends each logical message `redundancy` times with the
        // SAME seq. Deliver it ONCE; silently drop the redundant copies so callbacks
        // (and animations) never double-fire. Legitimate repeats get a NEW seq, so a
        // genuinely-repeated note/CC still passes through.
        uint8_t seq = buf[2];
        if (!(_haveMidiSeq && seq == _lastMidiSeq)) {
          _lastMidiSeq = seq;
          _haveMidiSeq = true;
          _rxCount++;
          dispatchMidi(buf[3], buf[4], buf[5]);   // applies the channel filter inside
        }
      } else if (h.type == sl::T_BEACON) {
        // Tempo rides every beacon. NOT deduped (beacons carry no seq) - each redundant
        // copy re-fires onSync, which is fine because the values are idempotent anchors.
        sl::BeaconInfo bi;
        if (sl::decodeBeacon(buf, len, bi)) {
          _rxBpm88   = bi.bpm88;
          _rxPhase15 = bi.phase15;
          _rxPlaying = bi.playing;
          if (_cbSync) _cbSync(bi.bpm88, bi.phase15, bi.playing);
        }
      }
    }

    if (_synced) {
      // Free-run between beacons (carries us through the odd lost beacon); each
      // beacon re-locks the phase above.
      if ((int32_t)(now - _nextHopMs) >= 0) {
        hopTo((uint8_t)((_hopPos + 1) % _count));
        _nextHopMs += _hopMs;
      }
      // Lost the master? Drop to scan.
      if ((uint32_t)(now - _lastRxMs) > (uint32_t)_hopMs * 5) {
        _synced         = false;
        _scanPos        = _hopPos;
        _scanStartMs    = now;
        _lastSeenHopPos = 0xFF;     // force a re-phase on the next packet heard
        _haveMidiSeq    = false;    // first MIDI after re-sync always delivers
      }
    } else {
      // Scan: park each channel for ~2 full master sweeps, so the master is
      // guaranteed to make a FRESH visit (dwell start -> beacon) while we're already
      // waiting -> clean lock. (>1 sweep also prevents a scan/sweep "chase".) We lock
      // above when a BEACON arrives; if a channel is jammed we step to the next.
      uint32_t dwell = (uint32_t)_hopMs * (uint32_t)_count * 2;
      if ((uint32_t)(now - _scanStartMs) >= dwell) {
        _scanPos     = (uint8_t)((_scanPos + 1) % _count);
        hopTo(_scanPos);
        _scanStartMs = now;
      }
    }
  }

  debugTick(now);
}

void RF24StageLink::debugTick(uint32_t now) {
  if (!_dbg) return;

  // Sync transitions (fixture) - event-driven, low noise.
  if (_role == ROLE_FIXTURE && _synced != _wasSynced) {
    _wasSynced = _synced;
    if (_synced) { _dbg->print(F("[RF] sync acquired ch=")); _dbg->println(currentChannel()); }
    else         { _dbg->println(F("[RF] sync lost - scanning")); }
  }

  // Heartbeat once a second.
  if ((uint32_t)(now - _dbgLastMs) >= 1000) {
    _dbgLastMs = now;
    if (_role == ROLE_MASTER) {
      _dbg->print(F("[RF] master ch=")); _dbg->print(currentChannel());
      _dbg->print(F(" tx="));            _dbg->print(_txCount);
      if (_absorbClock) {              // tempo tracker status (beat-sync mode)
        _dbg->print(F(" bpm="));  _dbg->print(((uint32_t)_bpm88 + 128) >> 8);  // Q8.8 -> nearest int
        _dbg->print(F(" play=")); _dbg->print(_playing);
      }
      _dbg->println();
    } else if (_role == ROLE_FIXTURE) {
      _dbg->print(F("[RF] fixture synced=")); _dbg->print(_synced);
      _dbg->print(F(" ch="));                 _dbg->print(currentChannel());
      _dbg->print(F(" pkt="));                _dbg->print(_pktCount);   // ALL packets heard
      _dbg->print(F(" rx="));                 _dbg->println(_rxCount);  // MIDI only
    }
  }
}

// ===========================================================================
// Master: send one MIDI message on the current channel.
//   broadcast mode -> NO_ACK, repeated `redundancy` times (returns true if accepted)
//   reliable mode  -> single ACK'd write, hardware retries (returns true if delivered)
// ===========================================================================
bool RF24StageLink::sendMidi(uint8_t b0, uint8_t b1, uint8_t b2) {
  if (_role != ROLE_MASTER) return false;

  // Beat-sync: absorb clock/transport locally and ride the tempo in the beacon instead
  // of relaying it. Returns true (handled) so the sketch's drain loop is satisfied, but
  // nothing goes on the air here. Only the four sync messages are swallowed; everything
  // else (incl. 0xFE/0xFF) falls through to normal handling.
  if (_absorbClock) {
    uint32_t now = millis();
    switch (b0) {
      case 0xF8: {                                  // Clock: 24 pulses per quarter note
        if (!_haveClock) {                          // first tick: seed the time bases
          _haveClock = true;
          _beatAnchorMs = now; _ppqnCount = 0;
          _tempoStartMs = now; _acqBeats = 0;       // arm acquisition
        }
        if (++_ppqnCount >= 24) {                    // one beat (24 ticks) elapsed
          _ppqnCount    = 0;
          _beatAnchorMs = now;                       // phase origin (downbeat) - keep this per-beat
          // PROGRESSIVE tempo lock. The window telescopes from the acquisition start (exact,
          // unbiased, immune to however USB batched the tick reads); GROWING the beat count is
          // what makes the first lock fast - a usable estimate at 2 beats (~0.7s, ~+/-7 BPM),
          // tightening each beat to ~+/-1 BPM by TEMPO_LOCK_BEATS, where it FREEZES (steady for
          // the rest of the song). A new song re-arms this via Start (0xFA). We keep the old
          // _bpm88 until the new 2-beat estimate lands, so fixtures don't flash to the fallback.
          if (_acqBeats < TEMPO_LOCK_BEATS) {
            _acqBeats++;
            uint32_t winMs = now - _tempoStartMs;
            if (_acqBeats >= 2 &&
                winMs >= (uint32_t)_acqBeats * 225 && winMs <= (uint32_t)_acqBeats * 3000) { // ~20..267 BPM
              // bpm88 (280-matched, beat88's 2^32/280) over _acqBeats beats = 15339169*_acqBeats / winMs.
              uint32_t inst88 = (15339169UL * _acqBeats) / winMs;
              if (inst88 > 0xFFFF) inst88 = 0xFFFF;  // saturate at the Q8.8 ceiling (~256 BPM)
              _bpm88 = (uint16_t)inst88;
            }
          }
        }
        return true;
      }
      case 0xFA: _playing = true;  _ppqnCount = 0; _beatAnchorMs = now;               // Start -> beat 0
                 _tempoStartMs = now; _acqBeats = 0; return true;  // re-arm fast lock (keep old _bpm88 until relock)
      case 0xFB: _playing = true;  return true;                                       // Continue
      case 0xFC: _playing = false; return true;                                       // Stop
      default: break;                                                                 // not a sync msg
    }
  }

  // TX noise filter: keep junk off the air.
  if (_dropActiveSensing   && b0 == 0xFE) return false;
  if (_dropMtcQuarterFrame && b0 == 0xF1) return false;
  _midiSeq++;                               // one new seq per LOGICAL message
  uint8_t buf[sl::MAX_PACKET];
  uint8_t n = sl::encodeMidi(buf, _hopPos, _midiSeq, b0, b1, b2);
  bool ok;
  if (_reliable) {
    ok = txCurrent(buf, n, /*noAck=*/false); // ACK + automatic hardware retries
  } else {
    // Every redundant copy carries the SAME seq -> the fixture delivers it once.
    ok = false;
    for (uint8_t i = 0; i < _redundancy; ++i) ok |= txCurrent(buf, n, /*noAck=*/true);
  }
  _txCount++;
  return ok;
}

// ===========================================================================
// Fixture: parse one MIDI message, apply the channel filter, fire callbacks
// ===========================================================================
void RF24StageLink::dispatchMidi(uint8_t b0, uint8_t b1, uint8_t b2) {
  if (b0 >= 0xF8) {                       // System Real-Time (no channel) - always pass
    if (_cbMidi) _cbMidi(b0, b1, b2);
    switch (b0) {
      case 0xF8: if (_cbClock)    _cbClock();    break;
      case 0xFA: if (_cbStart)    _cbStart();    break;
      case 0xFB: if (_cbContinue) _cbContinue(); break;
      case 0xFC: if (_cbStop)     _cbStop();     break;
      default: break;
    }
    return;
  }
  if (b0 >= 0xF0) {                       // System Common (SysEx/MTC/etc.) - raw only
    if (_cbMidi) _cbMidi(b0, b1, b2);
    return;
  }

  // Channel Voice: apply the channel filter (bit per channel, 0-based).
  uint8_t chan0 = b0 & 0x0F;
  if (!(_chanMask & (uint16_t)(1u << chan0))) return;

  if (_cbMidi) _cbMidi(b0, b1, b2);      // raw monitor (channel-filtered)

  uint8_t ch   = chan0 + 1;              // report 1..16
  uint8_t type = b0 & 0xF0;
  switch (type) {
    case 0x80: if (_cbNoteOff) _cbNoteOff(ch, b1, b2); break;
    case 0x90: if (b2 == 0) { if (_cbNoteOff) _cbNoteOff(ch, b1, 0); }    // vel 0 == Note Off
               else         { if (_cbNoteOn)  _cbNoteOn(ch, b1, b2); } break;
    case 0xB0: if (_cbControl) _cbControl(ch, b1, b2); break;
    case 0xC0: if (_cbProgram) _cbProgram(ch, b1);     break;
    case 0xE0: if (_cbPitch)   _cbPitch(ch, (int16_t)(((int)b2 << 7 | b1) - 8192)); break;
    // 0xA0 poly aftertouch, 0xD0 channel pressure: available via raw onMidi only
    default: break;
  }
}
