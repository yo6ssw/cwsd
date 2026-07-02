// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

//
// Remote paddle-keying wire format (operator -> rig).
//
// Real paddle keying cannot stream "an edge happened now" events: internet jitter would smear
// the spacing and destroy character formation. Instead the operator side timestamps every key
// transition against a per-session monotonic clock, and the rig side replays them behind a fixed
// playout delay (see remote_key_server). As long as a packet arrives before its scheduled playout
// time, the spacing between edges is reproduced exactly, independent of arrival jitter.
//
// Loss is dangerous here: a dropped "key up" edge would leave the transmitter keyed. Two defences
// live in the protocol; a third (hardware-style watchdog) lives in the replay engine:
//   1. Every packet carries the most recent edges (a short history), so a single lost datagram is
//      recovered by the next one. Edges are idempotent: the receiver dedups by source timestamp.
//   2. A packet with zero edges is a keepalive; the receiver forces a safe (key-up) state if the
//      stream goes silent.
//
// Multi-byte fields are big-endian, matching audio_stream_server's on-the-wire convention.
//

#ifndef CWSD_REMOTE_KEY_PROTOCOL_H
#define CWSD_REMOTE_KEY_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace remote_key {

    constexpr uint8_t  MAGIC   = 0xC7;          // 'cw' keying, version-checked below
    constexpr uint8_t  VERSION = 1;

    // packet.flags bits
    constexpr uint8_t  FLAG_SESSION_RESET = 0x01;   // operator restarted; drop anchor + force safe

    // edge.state bits
    constexpr uint8_t  KEY_DOWN = 0x01;         // bit set => key closed (transmitting an element)
    constexpr uint8_t  PTT_REQ  = 0x02;         // optional explicit PTT request (rig may derive its own)

    // A single key transition, timestamped on the operator's monotonic clock.
    struct edge {
        uint64_t ts_us;     // microseconds since the operator's session start
        uint8_t  state;     // bitfield of KEY_DOWN / PTT_REQ
    };

    struct packet {
        uint16_t          session_id = 0;   // random per operator session; a change re-anchors the rig
        uint8_t           flags      = 0;
        std::vector<edge> edges;            // ascending by ts_us; empty => pure keepalive
    };

    // Wire layout:
    //   header: magic(1) version(1) flags(1) session_id(2) edge_count(1)            = 6 bytes
    //   then edge_count * { ts_us(8) state(1) }                                     = 9 bytes each
    constexpr size_t HEADER_BYTES = 6;
    constexpr size_t EDGE_BYTES   = 9;

    // Keep history short enough to fit comfortably in one datagram while still bridging a few lost
    // packets. At 40 WPM there are well under ~100 edges/second, so even a handful of history edges
    // covers tens of milliseconds of loss.
    constexpr size_t MAX_EDGES = 32;

    constexpr size_t MAX_PACKET_BYTES = HEADER_BYTES + MAX_EDGES * EDGE_BYTES;

    namespace detail {
        inline void put_u16(uint8_t *p, uint16_t v) {
            p[0] = static_cast<uint8_t>(v >> 8);
            p[1] = static_cast<uint8_t>(v);
        }
        inline void put_u64(uint8_t *p, uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                p[i] = static_cast<uint8_t>(v >> (56 - 8 * i));
            }
        }
        inline uint16_t get_u16(const uint8_t *p) {
            return static_cast<uint16_t>(p[0]) << 8 | p[1];
        }
        inline uint64_t get_u64(const uint8_t *p) {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i) {
                v = v << 8 | p[i];
            }
            return v;
        }
    }

    // Serialize a packet. Edges beyond MAX_EDGES are dropped (callers send the most recent ones).
    inline std::vector<uint8_t> encode(const packet &pkt) {
        size_t n = pkt.edges.size() > MAX_EDGES ? MAX_EDGES : pkt.edges.size();
        std::vector<uint8_t> out(HEADER_BYTES + n * EDGE_BYTES);
        out[0] = MAGIC;
        out[1] = VERSION;
        out[2] = pkt.flags;
        detail::put_u16(&out[3], pkt.session_id);
        out[5] = static_cast<uint8_t>(n);

        // Send the *last* n edges so the freshest history wins when truncating.
        size_t first = pkt.edges.size() - n;
        uint8_t *e = &out[HEADER_BYTES];
        for (size_t i = 0; i < n; ++i) {
            detail::put_u64(e, pkt.edges[first + i].ts_us);
            e[8] = pkt.edges[first + i].state;
            e += EDGE_BYTES;
        }
        return out;
    }

    // Parse a datagram into `out`. Returns false on a malformed/foreign packet (caller ignores it).
    inline bool decode(const uint8_t *buf, size_t len, packet &out) {
        if (len < HEADER_BYTES || buf[0] != MAGIC || buf[1] != VERSION) {
            return false;
        }
        size_t count = buf[5];
        if (len < HEADER_BYTES + count * EDGE_BYTES) {
            return false;
        }
        out.flags = buf[2];
        out.session_id = detail::get_u16(&buf[3]);
        out.edges.clear();
        out.edges.reserve(count);
        const uint8_t *e = &buf[HEADER_BYTES];
        for (size_t i = 0; i < count; ++i) {
            out.edges.push_back(edge{detail::get_u64(e), e[8]});
            e += EDGE_BYTES;
        }
        return true;
    }

}

#endif //CWSD_REMOTE_KEY_PROTOCOL_H
