// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "morse.h"

#include <cstdio>

const uint8_t NUMBER_OF_DOTS_IN_PARIS = 10;
const uint8_t NUMBER_OF_SILENT_DOTS_IN_PARIS = 9;
const uint8_t NUMBER_OF_DASHES_IN_PARIS = 4;
const uint8_t NUMBER_OF_CHAR_SPACES_IN_PARIS = 4;
const float SECONDS_IN_A_MINUTE = 60.0f;
const float MILLISECONDS_IN_A_SECOND = 1000.0f;

float code_style::space_impulses_in_paris() {
  return static_cast<float>(NUMBER_OF_CHAR_SPACES_IN_PARIS *
                                impulses_between_characters +
                            impulses_between_words);
}

float code_style::non_space_impulses_in_paris() {
  return NUMBER_OF_DOTS_IN_PARIS + NUMBER_OF_SILENT_DOTS_IN_PARIS +
         NUMBER_OF_DASHES_IN_PARIS * dah_dit_ratio;
}

// Linearly interpolate the value to the given range.
// If out of bunds, then the closest bound is returned.
float map(float speed, float speed_min, float speed_max, float value_min,
          float value_max) {
  if (fabs(speed_max - speed_min) <= 0.001f) return value_min;
  if (speed <= speed_min) return value_min;
  if (speed >= speed_max) return value_max;
  auto alpha = (speed - speed_min) / (speed_max - speed_min);
  return value_min + alpha * (value_max - value_min);
}

code_timings code_style::calculate_timings() {
  //    printf("calculate timings\n");
  code_timings speed;

  // interpolate dynamic attributes
  dah_dit_ratio = map(character_wpm, speed_low, speed_high, dah_dit_ratio_low,
                      dah_dit_ratio_high);
  //    weighting = map(character_wpm, speed_low, speed_high, weighting_low,
  //    weighting_high);
  extra_letterspace_percent =
      map(character_wpm, speed_low, speed_high, extra_letterspace_percent_low,
          extra_letterspace_percent_high);

  float impulsesPerMinute = character_wpm * (non_space_impulses_in_paris() +
                                             space_impulses_in_paris());

  speed.dit_ms =
      (SECONDS_IN_A_MINUTE / impulsesPerMinute) * MILLISECONDS_IN_A_SECOND;
  speed.dah_ms = speed.dit_ms * dah_dit_ratio;
  speed.space_between_chars_ms =
      speed.dit_ms * static_cast<float>(impulses_between_characters);
  speed.space_between_words_ms =
      speed.dit_ms * static_cast<float>(impulses_between_words);

  float on_ms_per_minute =
      non_space_impulses_in_paris() * farnsworth_wpm * speed.dit_ms;

  float off_ms_per_minute =
      (SECONDS_IN_A_MINUTE * MILLISECONDS_IN_A_SECOND) - on_ms_per_minute;
  float pause_impulse_ms =
      off_ms_per_minute / (space_impulses_in_paris() * farnsworth_wpm);

  speed.space_between_chars_ms =
      static_cast<float>(impulses_between_characters) * pause_impulse_ms;
  speed.space_between_words_ms =
      static_cast<float>(impulses_between_words) * pause_impulse_ms;

  // apply weighting
  auto weight = static_cast<float>(weighting) / 50.0f;
  speed.weighted_dit_ms = speed.dit_ms * weight;
  speed.weighted_dah_ms = speed.dah_ms * weight;
  speed.weighted_inter_element_space_ms =
      speed.dit_ms * static_cast<float>(100 - weighting) / 50.0f;

  // apply keying compensation
  speed.weighted_dit_ms += keying_compensation_ms;
  speed.weighted_dah_ms += keying_compensation_ms;
  speed.weighted_inter_element_space_ms -= keying_compensation_ms;

  // add letterspace
  speed.weighted_inter_element_space_ms +=
      (3.0f * speed.weighted_dit_ms) * extra_letterspace_percent / 100.0f;

  return speed;
}

int ratio_to_int(float ratio) { return static_cast<int>(ratio * 50.0f / 3.0f); }

float ratio_from_int(int value) {
  return 3.0f * (static_cast<float>(value) / 50.0f);
}
