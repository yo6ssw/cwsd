// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "keyer_state_inter_element_space.h"
#include "keyer.h"

namespace keyer {

    void state_inter_element_space::enter() {
        time_to_exit_state = hardware->current_ms() + inter_element_space_length_ms();
    }

    state_type state_inter_element_space::update(uint32_t now_ms) {
        if (data.sent_element == element::dit) {
            check_dah_paddle();
        } else {
            check_dit_paddle();
        }

        if (now_ms >= time_to_exit_state) {
            if (data.winkeyer_enabled) {
                return state_type::winkeyer;
            } else if (data.is_playing) {
                return state_type::play;
            } else {
                poll_paddles();
                return (data.dit_buffered || data.dah_buffered || !get_current_profile().style.autospace_enabled) ?
                       state_type::idle :
                       state_type::autospace;
            }
        }

        return state_type::inter_element_space;
    }

}