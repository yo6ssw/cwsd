// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "keyer_state_play.h"

#include "keyer.h"
#include "keyer_char_patterns.h"

namespace keyer {

static char fetch_next_playing_char() {
  return data.memory[data.playing_slot][data.playing_char_index++];
}

state_type state_play::update(uint32_t elapsed_ms) {
  if (data.playing_bit == 0) {
    if (!inter_char_space_played) {
      inter_char_space_played = true;
      return state_type::autospace;
    }

    char c = fetch_next_playing_char();
    if (c == 0) {
      stop_playing();
      return state_type::idle;
    }

    auto pattern = get_char_pattern(c);
    if (pattern == nullptr) {
      return state_type::inter_word_space;
    } else {
      data.playing_data = pattern->sent_char_data;
      data.playing_bit = pattern->sent_char_bits;
      inter_char_space_played = false;
    }
  }

  auto mask = 1 << (data.playing_bit - 1);
  data.playing_bit--;
  data.sent_element =
      ((data.playing_data & mask) > 0) ? element::dah : element::dit;
  return state_type::send_element;
}

}  // namespace keyer