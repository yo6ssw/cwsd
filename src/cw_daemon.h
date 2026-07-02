// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#ifndef CW_DAEMON_H
#define CW_DAEMON_H

#include <cstdint>
#include <vector>

class cw_daemon {
 public:
  static std::vector<uint8_t> to_winkeyer(std::vector<uint8_t> input,
                                          uint8_t current_speed);

  static bool is_tuning_command(std::vector<uint8_t> message);
};

#endif  // CW_DAEMON_H
