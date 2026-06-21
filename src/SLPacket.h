/*
  SLPacket.h - RF24StageLink on-air packet format (frequency-hopping MIDI broadcast).

  Two tiny packet types, both little-endian and byte-serialized so the wire format
  is identical on every platform. No Arduino dependency (only <stdint.h>).

    byte0 : (PROTO_VERSION << 4) | type      // type: 0 = BEACON, 1 = MIDI
    byte1 : hopPos                           // current channel-sequence position (0..count-1)

  BEACON (6 bytes) - sent every hop; also carries tempo so animations can beat-sync
  WITHOUT relaying 24-ppqn MIDI clock over the air (the beacon flies ~20x/sec anyway):
    byte2 : phaseLo                          // beat phase, low byte  (0..32767 = fraction of one beat)
    byte3 : (phaseHi & 0x7F) | (playing<<7)  // 7 phase bits + transport-playing in bit7
    byte4 : bpmLo                            // tempo, Q8.8 BPM (accum88), low byte  (0 = unknown)
    byte5 : bpmHi                            // tempo, Q8.8 BPM (accum88), high byte

  MIDI (6 bytes):
    byte2 : seq                              // per-message sequence; redundant copies share it (for dedup)
    byte3..5 : b0, b1, b2                    // the three MIDI bytes

  The seq lets a fixture deliver each logical message EXACTLY ONCE even though the
  master transmits it `redundancy` times for loss-robustness (no-ACK broadcast).

  Copyright (C) 2026 bonjurroughs.  MIT License (see LICENSE).
*/
#ifndef RF24STAGELINK_SLPACKET_H
#define RF24STAGELINK_SLPACKET_H

#include <stdint.h>

namespace sl {

// Bumped 3 -> 4 when the beacon grew to carry tempo. readHeader() rejects any other
// version, so all nodes in a rig must run the same library version (existing contract).
static const uint8_t PROTO_VERSION = 4;
static const uint8_t MAX_PACKET    = 6;

enum Type : uint8_t { T_BEACON = 0, T_MIDI = 1 };

struct Header { uint8_t version; uint8_t type; uint8_t hopPos; };

// Decoded beacon tempo payload. bpm88 == 0 means "no tempo known" (clock absorb off,
// or no DAW clock yet); phase15 is the position within the current beat (0..32767).
struct BeaconInfo { uint8_t hopPos; uint16_t bpm88; uint16_t phase15; bool playing; };

inline uint8_t encodeBeacon(uint8_t* buf, uint8_t hopPos,
                            uint16_t bpm88, uint16_t phase15, bool playing) {
  buf[0] = (uint8_t)((PROTO_VERSION << 4) | T_BEACON);
  buf[1] = hopPos;
  buf[2] = (uint8_t)(phase15 & 0xFF);                                   // phase low 8 bits
  buf[3] = (uint8_t)(((phase15 >> 8) & 0x7F) | (playing ? 0x80 : 0x00)); // phase hi 7 + playing
  buf[4] = (uint8_t)(bpm88 & 0xFF);
  buf[5] = (uint8_t)(bpm88 >> 8);
  return 6;
}

inline uint8_t encodeMidi(uint8_t* buf, uint8_t hopPos, uint8_t seq,
                          uint8_t b0, uint8_t b1, uint8_t b2) {
  buf[0] = (uint8_t)((PROTO_VERSION << 4) | T_MIDI);
  buf[1] = hopPos;
  buf[2] = seq;
  buf[3] = b0; buf[4] = b1; buf[5] = b2;
  return 6;
}

// Returns false on a short buffer or protocol-version mismatch.
inline bool readHeader(const uint8_t* buf, uint8_t len, Header& h) {
  if (len < 2) return false;
  h.version = (uint8_t)(buf[0] >> 4);
  h.type    = (uint8_t)(buf[0] & 0x0F);
  h.hopPos  = buf[1];
  return h.version == PROTO_VERSION;
}

// Returns false if the beacon is too short to carry tempo (older/short beacon).
inline bool decodeBeacon(const uint8_t* buf, uint8_t len, BeaconInfo& info) {
  if (len < 6) return false;
  info.hopPos  = buf[1];
  info.phase15 = (uint16_t)(((uint16_t)(buf[3] & 0x7F) << 8) | buf[2]);
  info.playing = (buf[3] & 0x80) != 0;
  info.bpm88   = (uint16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
  return true;
}

} // namespace sl

#endif // RF24STAGELINK_SLPACKET_H
