// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

//
// Rig-side replay engine for remote paddle keying.
//
// Receives timestamped key edges (see remote_key_protocol.h) over UDP and
// replays them onto the rig's serial control lines (DTR = key, RTS = PTT, as in
// cwdaemon_server's key_interface) behind a fixed playout delay that absorbs
// network jitter. A dedicated real-time thread schedules each edge with
// clock_nanosleep(TIMER_ABSTIME) so element timing does not depend on the 2 ms
// main loop.
//
// Unlike cwdaemon_server this path runs no keyer state machine: the operator's
// client forms the elements (iambic logic + sidetone live there, on jitter-free
// local input) and streams finished edges. A straight key is just the
// degenerate case of raw edges.
//

#ifndef CWSD_REMOTE_KEY_SERVER_H
#define CWSD_REMOTE_KEY_SERVER_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

struct remote_key_config {
  bool enabled = false;
  int port = 0;        // UDP port to bind for the edge stream
  std::string device;  // serial device with the keying control lines (DTR/RTS)

  uint32_t playout_ms =
      150;  // jitter-buffer depth: how far the rig lags the operator
  uint32_t silence_ms =
      250;  // force key-up if no packet (incl. keepalive) for this long
  uint32_t max_key_down_ms =
      5000;  // hard watchdog: never hold the key down longer than this
  uint32_t ptt_lead_ms = 10;   // assert PTT this long before the first key-down
  uint32_t ptt_tail_ms = 100;  // hold PTT this long after the last key-up
};

class remote_key_server {
 public:
  explicit remote_key_server(remote_key_config cfg);
  ~remote_key_server();

  void update();  // no-op; cadence is owned by the replay thread
  void stop();

 private:
  // One scheduled edge, already converted to the local CLOCK_MONOTONIC
  // timeline.
  struct scheduled_edge {
    uint64_t at_us;  // local monotonic time to apply this edge
    uint8_t state;   // remote_key::KEY_DOWN / PTT_REQ
  };

  void receive_worker();  // socket -> jitter buffer
  void replay_worker();   // jitter buffer -> hardware, with safety watchdogs

  // --- serial keying (DTR = key, RTS = PTT) ---
  bool open_port();
  void set_key(bool down);
  void set_ptt(bool on);
  void force_safe();  // key up + PTT off, unconditionally

  remote_key_config cfg;

  int sockfd = -1;
  int fd = -1;      // serial device fd
  int key_bit = 0;  // TIOCM_DTR
  int ptt_bit = 0;  // TIOCM_RTS

  // Jitter buffer + session anchor, shared between the two worker threads.
  std::mutex mtx;
  std::deque<scheduled_edge> queue;
  bool have_anchor = false;
  uint64_t anchor_src_us =
      0;  // operator timestamp of the first edge this session
  uint64_t anchor_local_us = 0;  // local time we chose to play that first edge
  uint64_t last_enqueued_src = 0;  // dedup highwater (operator timeline)
  uint16_t session_id = 0;
  bool have_session = false;

  std::atomic<uint64_t> last_rx_us{
      0};  // local time of the last received datagram
  std::atomic<bool> reset_requested{false};

  std::atomic<bool> is_running{false};
  std::thread rx_thread;
  std::thread replay_thread;
};

#endif  // CWSD_REMOTE_KEY_SERVER_H
