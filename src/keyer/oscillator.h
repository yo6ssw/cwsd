// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#pragma once

#include <cstdint>

enum class wave_type : uint8_t {
  sine = 0,
  triangle = 1,
  sawtooth = 2,
  square = 3,
};

class oscillator {
 public:
  explicit oscillator(float samplerate = 96000.0f);
  void set_wave_type(wave_type type_);
  void set_frequency(float frequency);
  void reset();
  int16_t tick();
  int16_t get_at_phase_offset(uint32_t phase_offset);

 private:
  float samplerate = 96000.0f;
  uint32_t phase = 0;
  uint32_t phase_add = 0;
  int16_t tables[4][1024];
  wave_type type = wave_type::sine;
  uint8_t table_index = 0;
};
