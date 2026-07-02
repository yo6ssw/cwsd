// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#ifndef CWSD_TIMER_H
#define CWSD_TIMER_H

#include <chrono>
#include <cstdint>

class timer {
 public:
  timer();
  uint32_t ellapsed_ms() const;

 private:
  std::chrono::time_point<std::chrono::system_clock> start;
};

#endif  // CWSD_TIMER_H
