// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "keyer_state_half_dot_gap.h"
#include "keyer.h"

namespace keyer {

    void state_half_dot_gap::enter() {
        time_to_exit_state = hardware->current_ms() + weighted_element_length_ms(element::dit) / 2;
    }

    state_type state_half_dot_gap::update(uint32_t elapsed_ms) {
        auto now = hardware->current_ms();
        return now >= time_to_exit_state ?
               (data.is_playing ? state_type::play : (data.winkeyer_enabled ? state_type::winkeyer
                                                                            : state_type::idle)) :
               state_type::half_dot_gap;
    }

}