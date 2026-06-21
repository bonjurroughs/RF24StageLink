/*
  RF24StageLink.h - frequency-hopping USB-MIDI broadcast over nRF24L01(+).

  One MASTER reads live USB-MIDI and broadcasts it; many FIXTURES listen.
  Master and fixtures hop through the same channel list in lockstep (FHSS) to ride
  out 2.4GHz interference. Pure one-to-many broadcast (no ACK, no per-fixture
  addressing) - every fixture receives the whole MIDI stream.

  How sync works (kept deliberately simple): the master sends a tiny BEACON every
  hop carrying the hop position; MIDI packets carry it too. A fixture adopts the
  hop position from any packet it hears and tracks the schedule on its own timer,
  re-aligning on every beacon. If it stops hearing the master it scans the channel
  list until it re-acquires. No clock PLL, no tempo, no adaptive scanning.

  Layered on RF24 by composition - never modifies RF24. Cross-platform; all timing
  is a non-blocking update() pump (millis()), no AVR timers/ISRs.

  Copyright (C) 2026 bonjurroughs.  MIT License (see LICENSE).
  Combined firmware (linked with GPL-2.0-only RF24) is GPL-2.0-only.
*/
#ifndef RF24STAGELINK_H
#define RF24STAGELINK_H

#include <Arduino.h>
#include <RF24.h>
#include "SLPacket.h"

class RF24StageLink {
public:
  typedef void (*MidiCallback)(uint8_t b0, uint8_t b1, uint8_t b2);  // raw catch-all

  // Decoded "typed" MIDI callbacks. Channel is 1..16 (DAW convention).
  typedef void (*SLNoteCb)     (uint8_t channel, uint8_t note, uint8_t velocity);
  typedef void (*SLControlCb)  (uint8_t channel, uint8_t control, uint8_t value);
  typedef void (*SLProgramCb)  (uint8_t channel, uint8_t program);
  typedef void (*SLPitchBendCb)(uint8_t channel, int16_t bend);     // -8192..+8191 (centered)
  typedef void (*SLRealtimeCb) ();                                  // clock/start/stop/continue (no channel)
  // Beat-sync callback (fixture): fires once per beacon (~20/sec while synced).
  //   bpm88   = tempo in Q8.8 BPM (0 = unknown). phase15 = position in the beat (0..32767).
  //   playing = transport running. Feed these to SLAnimator::onSync for phase-locked animation.
  typedef void (*SLSyncCb)     (uint16_t bpm88, uint16_t phase15, bool playing);

  static const uint8_t MAX_CHANNELS = 16;

  explicit RF24StageLink(RF24& radio);

  // ---- configuration (call before beginMaster/beginFixture) ----
  void setChannels(const uint8_t* channels, uint8_t count); // hop set (1..MAX_CHANNELS)
  void setRadioAddress(const uint8_t* addr5);               // shared broadcast address
  void setHopInterval(uint16_t ms)   { _hopMs = ms ? ms : 1; }
  void setDataRate(rf24_datarate_e d){ _dataRate = d; }
  void setPALevel(uint8_t level)     { _paLevel = level; }
  void setRedundancy(uint8_t n)      { _redundancy = n ? n : 1; }
  // Reliable point-to-point mode: auto-ACK + hardware retries on MIDI (guaranteed
  // delivery, and sendMidi() reports it). ONE fixture ONLY - multiple fixtures would
  // collide on the ACK. Must be set identically on the master AND the fixture.
  // Default false = broadcast to many fixtures (no ACK). (Beacons stay NO_ACK either way.)
  void setReliable(bool r)           { _reliable = r; }

  // ---- roles ----
  bool beginMaster();
  bool beginFixture();

  // ---- pump: call as often as possible from loop() ----
  void update();

  // ---- master ----
  // Returns delivery status: broadcast mode -> true if the write was accepted;
  // reliable mode -> true if the fixture ACK'd it (false = not delivered).
  bool sendMidi(uint8_t b0, uint8_t b1, uint8_t b2);
  // TX noise filter (master): keep junk off the air. Defaults: drop Active Sensing.
  void setDropActiveSensing(bool d)   { _dropActiveSensing = d; }   // 0xFE (default true)
  void setDropMtcQuarterFrame(bool d) { _dropMtcQuarterFrame = d; } // 0xF1 (default false)
  // Beat-sync mode (master): when true, sendMidi() ABSORBS MIDI clock/transport
  // (0xF8 Clock, 0xFA Start, 0xFB Continue, 0xFC Stop) - it derives the tempo locally
  // and rides it in the beacon instead of relaying 24 clock bytes/beat over the air.
  // Fixtures get tempo via onSync (NOT onClock/onStart - those stop firing). Default
  // false = legacy behavior (clock forwarded as MIDI, beacon carries no tempo).
  void setAbsorbClock(bool a)         { _absorbClock = a; }

  // ---- fixture: typed MIDI callbacks (subscribe only to what you need) ----
  void onNoteOn(SLNoteCb cb)         { _cbNoteOn  = cb; }
  void onNoteOff(SLNoteCb cb)        { _cbNoteOff = cb; }
  void onControlChange(SLControlCb cb){ _cbControl = cb; }
  void onProgramChange(SLProgramCb cb){ _cbProgram = cb; }
  void onPitchBend(SLPitchBendCb cb) { _cbPitch   = cb; }
  void onClock(SLRealtimeCb cb)      { _cbClock   = cb; }
  void onStart(SLRealtimeCb cb)      { _cbStart   = cb; }
  void onStop(SLRealtimeCb cb)       { _cbStop    = cb; }
  void onContinue(SLRealtimeCb cb)   { _cbContinue= cb; }
  // raw catch-all (every message that passes the channel filter; also SysEx/MTC/etc.)
  void onMidi(MidiCallback cb) { _cbMidi = cb; }
  // Beat-sync: fires per beacon with the master's tempo/phase (requires the master to
  // run setAbsorbClock(true)). Wire straight to SLAnimator::onSync for phase-locked LEDs.
  void onSync(SLSyncCb cb)     { _cbSync = cb; }

  // ---- fixture: poll the last sync values (instead of / alongside onSync) ----
  uint16_t syncBpm88()  const { return _rxBpm88; }     // Q8.8 BPM, 0 = none yet
  uint16_t syncPhase15()const { return _rxPhase15; }   // position in beat, 0..32767
  bool     syncPlaying()const { return _rxPlaying; }   // transport running
  bool     haveTempo()  const { return _rxBpm88 != 0; }

  // ---- fixture: MIDI-channel filter (applies to channel-voice messages) ----
  void setChannelMask(uint16_t mask) { _chanMask = mask; }   // bit (ch-1) set = listen
  void listenChannel(uint8_t ch)     { _chanMask = (ch >= 1 && ch <= 16) ? (uint16_t)(1u << (ch - 1)) : 0xFFFF; }
  void listenAllChannels()           { _chanMask = 0xFFFF; }

  bool isSynced() const { return _synced; }

  // ---- status ----
  uint8_t currentChannel() const { return _channels[_hopPos]; }

  // ---- optional diagnostics (opt-in; off unless you call setDebugStream) ----
  // Logs radio status to a stream once a second + on sync changes, e.g.:
  //   [RF] fixture synced=1 ch=80 rx=42        (fixture)
  //   [RF] master ch=74 tx=128                 (master)
  // Call Serial.begin() first. This does NOT print MIDI - that's your sketch's job.
  void setDebugStream(Stream& s) { _dbg = &s; }
  void disableDebug()            { _dbg = 0; }

private:
  enum Role { ROLE_NONE, ROLE_MASTER, ROLE_FIXTURE };

  void applyRadioConfig();
  void hopTo(uint8_t pos);                 // set radio to channels[pos]
  bool txCurrent(const uint8_t* buf, uint8_t len, bool noAck);  // returns delivery success
  void debugTick(uint32_t now);
  void dispatchMidi(uint8_t b0, uint8_t b1, uint8_t b2);  // parse + filter + fire callbacks

  RF24&           _radio;
  Role            _role;

  uint8_t         _channels[MAX_CHANNELS];
  uint8_t         _count;
  uint8_t         _hopPos;
  uint8_t         _addr[5];
  rf24_datarate_e _dataRate;
  uint8_t         _paLevel;
  uint16_t        _hopMs;
  uint8_t         _redundancy;
  bool            _reliable;       // true = point-to-point auto-ACK (one fixture); false = broadcast

  uint32_t        _lastHopMs;     // master: time of last hop ; fixture: next-hop deadline base

  // fixture runtime
  bool            _synced;
  uint32_t        _nextHopMs;
  uint32_t        _lastRxMs;
  uint8_t         _scanPos;
  uint32_t        _scanStartMs;
  uint8_t         _lastSeenHopPos;   // master's hopPos from the last packet (dwell-change detect)

  // MIDI redundancy / dedup
  uint8_t         _midiSeq;          // master: seq stamped on each logical message
  uint8_t         _lastMidiSeq;      // fixture: seq of the last DELIVERED message
  bool            _haveMidiSeq;      // fixture: false until the first MIDI is delivered

  // MIDI dispatch
  MidiCallback    _cbMidi;
  SLNoteCb        _cbNoteOn, _cbNoteOff;
  SLControlCb     _cbControl;
  SLProgramCb     _cbProgram;
  SLPitchBendCb   _cbPitch;
  SLRealtimeCb    _cbClock, _cbStart, _cbStop, _cbContinue;
  uint16_t        _chanMask;
  bool            _dropActiveSensing;
  bool            _dropMtcQuarterFrame;

  // beat sync - master tempo tracker (derived from the DAW's MIDI clock)
  bool            _absorbClock;      // true = swallow clock/transport, ride tempo in the beacon
  uint16_t        _bpm88;            // tracked tempo, Q8.8 (0 = unknown); broadcast in the beacon
  bool            _haveClock;        // false until the first 0xF8
  uint8_t         _ppqnCount;        // 0..23 clock ticks within the current beat (phase)
  uint32_t        _beatAnchorMs;     // time of the last beat boundary (phase origin)
  uint32_t        _tempoStartMs;     // acquisition start: window the BPM telescopes from
  uint8_t         _acqBeats;         // beats since acquisition start (caps at TEMPO_LOCK_BEATS)
  bool            _playing;          // transport state (Start/Continue vs Stop)

  // beat sync - fixture receive state
  SLSyncCb        _cbSync;
  uint16_t        _rxBpm88;
  uint16_t        _rxPhase15;
  bool            _rxPlaying;

  // diagnostics
  Stream*         _dbg;
  bool            _wasSynced;
  uint32_t        _rxCount;       // MIDI messages received (fixture)
  uint32_t        _pktCount;      // ALL packets received incl. beacons (fixture, diagnostics)
  uint32_t        _txCount;       // MIDI messages broadcast (master)
  uint32_t        _dbgLastMs;
};

#endif // RF24STAGELINK_H
