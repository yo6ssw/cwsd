// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "keyer_state_inter_word_space.h"

#include "keyer.h"

namespace keyer {

void state_inter_word_space::enter() {
  time_to_exit = data.last_send_time + inter_word_space_length_ms();
  hardware->on_char_sent(' ');
}

state_type state_inter_word_space::update(uint32_t elapsed_ms) {
  poll_paddles();
  auto now = hardware->current_ms();
  if (now >= time_to_exit) {
    // allow more spaces one after the other
    data.last_send_time = now;
    return data.winkeyer_enabled ? state_type::winkeyer : state_type::play;
  }
  return state_type::inter_word_space;
}

}  // namespace keyer