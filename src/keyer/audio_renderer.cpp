// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "audio_renderer.h"

#include <cmath>

namespace keyer {

[[maybe_unused]] renderer::renderer(float samplerate_)
    : osc{samplerate_}, samplerate{samplerate_} {}

void renderer::key_down() {
  key_on = true;
  osc.reset();
  ramp_index = 0;
}

void renderer::key_up() {
  key_on = false;
  ramp_index = 0;
}

void renderer::set_wave_type(wave_type type) { osc.set_wave_type(type); }

void renderer::set_frequency(uint16_t frequency) {
  osc.set_frequency(frequency);
}

void renderer::update_amp() {
  amp = key_on ? 1 : 0;
  if (key_on && ramp_index < ramp_up_samples) {
    float alpha =
        static_cast<float>(ramp_index) / static_cast<float>(ramp_up_samples);
    amp = sinf(alpha * (float)M_PI / 2.0f);  // 0 to PI/2
    ramp_index++;
  } else if (!key_on && ramp_index < ramp_down_samples) {
    float alpha =
        static_cast<float>(ramp_index) / static_cast<float>(ramp_down_samples);
    amp = cosf(alpha * (float)M_PI / 2.0f);  // 0 to PI/2
    ramp_index++;
  }
}

void renderer::tick(int16_t& left, int16_t& right) {
  update_amp();
  auto volume = amp * level;
  left = static_cast<int16_t>(volume * osc.tick());
  //        right = volume * osc.get_at_phase_offset(0xffffffff >> 4);
  static uint32_t phase_offset = 0;
  phase_offset += 5000;
  right = static_cast<int16_t>(volume * osc.get_at_phase_offset(phase_offset));
}

void renderer::set_level(float value) { level = value; }

void renderer::set_ramp_up_time(float ms) {
  ramp_up_samples = static_cast<uint16_t>((samplerate * ms) / 1000.0f);
}

void renderer::set_ramp_down_time(float ms) {
  ramp_down_samples = static_cast<uint16_t>((samplerate * ms) / 1000.0f);
}
}  // namespace keyer