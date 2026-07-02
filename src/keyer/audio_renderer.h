// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#pragma once

#include "oscillator.h"

namespace keyer {

struct renderer {
  [[maybe_unused]] explicit renderer(float samplerate_ = 96000.0f);

  void set_wave_type(wave_type type);
  void set_frequency(uint16_t frequency);
  void set_level(float value);
  void set_ramp_up_time(float ms);
  void set_ramp_down_time(float ms);

  void key_down();
  void key_up();

  void tick(int16_t& left, int16_t& right);

 private:
  void update_amp();

  oscillator osc;
  float samplerate;
  bool key_on = false;

  float level = 0.5f;
  float amp = 1.0f;
  uint16_t ramp_up_samples = 0;
  uint16_t ramp_down_samples = 0;
  uint16_t ramp_index = 0;
};

}  // namespace keyer