// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#ifndef CWSD_CWSD_H
#define CWSD_CWSD_H

#include <atomic>
#include <csignal>
#include <string>

#include "audio_stream_server.h"
#include "cwdaemon_server.h"
#include "remote_key_server.h"
#include "rigctld_server.h"

struct cwsd_config {
  struct {
    bool enabled = false;
    int port;
    int initial_wpm = 30;
  } cwdaemon;
  struct {
    bool enabled = false;
    int port;
  } rigctld;
  struct {
    std::string port;
    int model;
  } rig;
  audio_stream_config audio;
  remote_key_config remote_key;
  struct {
    std::string filename = "cwsd.log";
    std::string level = "info";
    uint32_t max_size =
        1048576;  // max size in bytes for logrotate. default 1MB
  } logging;
};

class cwsd {
 public:
  void initialize_signal_handler();
  explicit cwsd(cwsd_config cfg);
  ~cwsd();

  static void signal_handler(int s);

  void run();

 private:
  cwsd_config config{};
  static std::atomic<bool> is_running;
  rigctld_server* rigctld = nullptr;
  cwdaemon_server* cwdaemon = nullptr;
  audio_stream_server* audio = nullptr;
  remote_key_server* remote_key = nullptr;
  struct sigaction sigint_hndlr;
};

#endif  // CWSD_CWSD_H
