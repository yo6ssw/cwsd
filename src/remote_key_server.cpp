// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

//
// Created for remote paddle keying. See remote_key_server.h for the design
// rationale.
//

#include "remote_key_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "libs/easylogging++.h"
#include "remote_key_protocol.h"

namespace {
// Local replay clock. CLOCK_MONOTONIC matches clock_nanosleep(TIMER_ABSTIME)
// below.
uint64_t now_us() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000ull + ts.tv_nsec / 1000;
}

void sleep_until_us(uint64_t t_us) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(t_us / 1000000ull);
  ts.tv_nsec = static_cast<long>((t_us % 1000000ull) * 1000ull);
  // EINTR-restart is handled by passing TIMER_ABSTIME and re-sleeping to the
  // same target.
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) ==
         EINTR) {
  }
}

// Same real-time promotion the keyer thread uses; replayed elements need it
// just as much.
void try_set_realtime_priority() {
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    LOG(WARNING) << "remote_key: mlockall failed (" << std::strerror(errno)
                 << "); keying may jitter under memory pressure";
  }
  sched_param sp{};
  sp.sched_priority = sched_get_priority_min(SCHED_FIFO) + 10;
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
    LOG(WARNING) << "remote_key: could not enable real-time scheduling ("
                 << std::strerror(errno)
                 << "); run as root or grant CAP_SYS_NICE";
  } else {
    LOG(INFO) << "remote_key: replay thread running at real-time priority "
              << sp.sched_priority;
  }
}
}  // namespace

remote_key_server::remote_key_server(remote_key_config c) : cfg(std::move(c)) {
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    throw std::runtime_error("remote_key: socket creation failed");
  }

  // Short receive timeout so the rx thread can also notice shutdown promptly.
  timeval to{};
  to.tv_sec = 0;
  to.tv_usec = 50000;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(cfg.port));
  if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(sockfd);
    sockfd = -1;
    throw std::runtime_error("remote_key: bind failed");
  }
  LOG(INFO) << "remote_key listening on port " << cfg.port;

  is_running = true;
  rx_thread = std::thread(&remote_key_server::receive_worker, this);
  replay_thread = std::thread(&remote_key_server::replay_worker, this);
}

remote_key_server::~remote_key_server() { stop(); }

// ---------------------------------------------------------------------------
// serial keying
// ---------------------------------------------------------------------------

bool remote_key_server::open_port() {
  fd = open(cfg.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    return false;
  }
  key_bit = TIOCM_DTR;
  ptt_bit = TIOCM_RTS;
  ioctl(fd, TIOCMBIC, &key_bit);  // start in a safe, un-keyed state
  ioctl(fd, TIOCMBIC, &ptt_bit);
  LOG(INFO) << "remote_key connected to " << cfg.device;
  return true;
}

void remote_key_server::set_key(bool down) {
  if (fd < 0) return;
  ioctl(fd, down ? TIOCMBIS : TIOCMBIC, &key_bit);
}

void remote_key_server::set_ptt(bool on) {
  if (fd < 0) return;
  ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &ptt_bit);
}

void remote_key_server::force_safe() {
  set_key(false);
  set_ptt(false);
}

// ---------------------------------------------------------------------------
// receive: socket -> jitter buffer
// ---------------------------------------------------------------------------

void remote_key_server::receive_worker() {
  uint8_t buf[remote_key::MAX_PACKET_BYTES];

  while (is_running) {
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) {
      continue;  // timeout / EAGAIN; loop also re-checks is_running
    }

    remote_key::packet pkt;
    if (!remote_key::decode(buf, static_cast<size_t>(n), pkt)) {
      continue;  // foreign or malformed datagram
    }

    uint64_t now = now_us();
    last_rx_us.store(now, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mtx);

    // A new session (operator restart) or an explicit reset re-anchors and
    // drops anything queued.
    bool new_session = !have_session || pkt.session_id != session_id;
    if (new_session || (pkt.flags & remote_key::FLAG_SESSION_RESET)) {
      session_id = pkt.session_id;
      have_session = true;
      have_anchor = false;
      last_enqueued_src = 0;
      queue.clear();
      reset_requested.store(true,
                            std::memory_order_relaxed);  // replay forces key-up
      LOG(INFO) << "remote_key: new keying session " << session_id;
    }

    for (const auto& e : pkt.edges) {
      // Dedup FIRST: last_enqueued_src is monotonic within a session and is
      // only reset on a genuine new session (above), never on a silence
      // re-anchor. So an edge we already played is always dropped here --
      // crucially, the stale history that keepalives keep re-sending is never
      // replayed after a silence gap re-anchors the playout clock.
      if (e.ts_us <= last_enqueued_src) {
        continue;  // already have it (history overlap / duplicate) ->
                   // idempotent
      }
      if (!have_anchor) {
        // Anchor on the first genuinely-new edge (not a stale history edge):
        // map the operator timeline to local time offset by the playout delay.
        // Every subsequent edge is scheduled relative to this pair, so spacing
        // is preserved.
        anchor_src_us = e.ts_us;
        anchor_local_us = now + static_cast<uint64_t>(cfg.playout_ms) * 1000ull;
        have_anchor = true;
      }
      last_enqueued_src = e.ts_us;

      uint64_t scheduled = anchor_local_us + (e.ts_us - anchor_src_us);
      if (scheduled < now) {
        // Arrived later than its playout slot: the buffer is too shallow for
        // this link. Play immediately rather than drop it; spacing slips but
        // the element is not lost.
        // TODO: adaptively grow cfg.playout_ms when this happens repeatedly.
        scheduled = now;
      }
      queue.push_back({scheduled, e.state});
    }
  }
}

// ---------------------------------------------------------------------------
// replay: jitter buffer -> hardware, with safety watchdogs
// ---------------------------------------------------------------------------

void remote_key_server::replay_worker() {
  try_set_realtime_priority();

  if (!open_port()) {
    LOG(WARNING) << "remote_key: " << cfg.device
                 << " not available yet; will retry";
  }

  bool key_is_down = false;
  uint64_t key_down_since_us = 0;
  bool ptt_is_on = false;
  uint64_t ptt_release_at_us = 0;
  uint64_t last_iface_check = 0;

  const uint64_t lead_us = static_cast<uint64_t>(cfg.ptt_lead_ms) * 1000ull;
  const uint64_t tail_us = static_cast<uint64_t>(cfg.ptt_tail_ms) * 1000ull;
  const uint64_t max_down_us =
      static_cast<uint64_t>(cfg.max_key_down_ms) * 1000ull;
  const uint64_t silence_us = static_cast<uint64_t>(cfg.silence_ms) * 1000ull;

  while (is_running) {
    uint64_t now = now_us();

    // Reconnect the serial device if it went away (USB unplug), roughly every
    // 100 ms.
    if (fd < 0 && now - last_iface_check > 100000ull) {
      last_iface_check = now;
      open_port();
    }

    // --- safety: session reset forces an immediate safe state ---
    if (reset_requested.exchange(false, std::memory_order_relaxed)) {
      force_safe();
      key_is_down = ptt_is_on = false;
    }

    // --- safety: stream silence forces key-up + PTT-off ---
    uint64_t since_rx = now - last_rx_us.load(std::memory_order_relaxed);
    if (last_rx_us.load(std::memory_order_relaxed) != 0 &&
        since_rx > silence_us) {
      if (key_is_down || ptt_is_on) {
        LOG(WARNING) << "remote_key: stream silent for " << since_rx / 1000
                     << " ms; forcing safe state";
        force_safe();
        key_is_down = ptt_is_on = false;
      }
      std::lock_guard<std::mutex> lock(mtx);
      queue.clear();
      have_anchor = false;  // re-anchor when the stream resumes
    }

    // --- safety: hard watchdog against a stuck key (e.g. a lost key-up edge)
    // ---
    if (key_is_down && now - key_down_since_us > max_down_us) {
      LOG(ERROR) << "remote_key: key held down > " << cfg.max_key_down_ms
                 << " ms; forcing key-up (lost key-up edge?)";
      set_key(false);
      key_is_down = false;
    }

    // --- apply all edges now due, and peek the next one for
    // scheduling/PTT-lead ---
    bool have_next = false;
    uint64_t next_at = 0;
    uint8_t next_state = 0;
    {
      std::lock_guard<std::mutex> lock(mtx);
      while (!queue.empty() && queue.front().at_us <= now) {
        scheduled_edge se = queue.front();
        queue.pop_front();

        bool down = (se.state & remote_key::KEY_DOWN) != 0;
        if (down && !ptt_is_on) {
          set_ptt(true);  // lead window may have been missed; assert now at the
                          // latest
          ptt_is_on = true;
        }
        if (down != key_is_down) {
          set_key(down);
          key_is_down = down;
          if (down) {
            key_down_since_us = now;
          } else {
            ptt_release_at_us = now + tail_us;
          }
        }
      }
      if (!queue.empty()) {
        have_next = true;
        next_at = queue.front().at_us;
        next_state = queue.front().state;
      }
    }

    // --- PTT sequencing: lead before the next key-down, tail after the last
    // key-up ---
    if (have_next && (next_state & remote_key::KEY_DOWN) && !ptt_is_on) {
      if (now + lead_us >= next_at) {
        set_ptt(true);
        ptt_is_on = true;
      }
    }
    if (ptt_is_on && !key_is_down && now >= ptt_release_at_us) {
      // Don't drop PTT if another key-down is already imminent (mid-character
      // gap).
      bool keydown_pending = have_next && (next_state & remote_key::KEY_DOWN) &&
                             next_at < now + tail_us;
      if (!keydown_pending) {
        set_ptt(false);
        ptt_is_on = false;
      }
    }

    // --- decide how long to sleep: until the next edge, but cap so watchdogs
    // keep ticking ---
    uint64_t wake = now + 5000ull;  // 5 ms safety/watchdog tick
    if (have_next && next_at < wake) {
      wake = next_at;
    }
    if (ptt_is_on && !key_is_down && ptt_release_at_us < wake) {
      wake = ptt_release_at_us;
    }
    if (wake > now) {
      sleep_until_us(wake);
    }
  }

  force_safe();
}

// ---------------------------------------------------------------------------

void remote_key_server::update() {
  // Keying cadence is owned by replay_worker(); nothing to do on the main loop.
}

void remote_key_server::stop() {
  if (!is_running.exchange(false)) {
    return;
  }
  if (rx_thread.joinable()) rx_thread.join();
  if (replay_thread.joinable()) replay_thread.join();
  if (sockfd >= 0) {
    close(sockfd);
    sockfd = -1;
  }
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
  LOG(INFO) << "remote_key server shut down";
}
