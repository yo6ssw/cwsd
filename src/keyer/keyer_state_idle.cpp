// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "keyer_state_idle.h"
#include "keyer.h"

namespace keyer {

    static element opposite_element(element el) {
        return el == element::none ? element::none : (el == element::dah ? element::dit : element::dah);
    }

    uint32_t time_to_decode;

    void state_idle::enter() {
        auto& profile = get_current_profile();
        time_for_word_space = data.last_send_time
                              + profile.timings.space_between_words_ms
                              - profile.timings.weighted_inter_element_space_ms * 1.5f;
        time_to_decode = data.last_send_time
                         + profile.timings.weighted_inter_element_space_ms * 1.05f;
    }

    state_type state_idle::update(uint32_t now_ms) {
        poll_paddles();


        if (data.dit_buffered && data.dah_buffered) {
            if (data.sent_element == element::none) {
                data.sent_element = element::dit;
            } else {
                data.sent_element = opposite_element(data.sent_element);
            }
            return state_type::send_element;
        }

        if (data.dit_buffered || data.dah_buffered) {
            data.sent_element = data.dit_buffered ? element::dit : element::dah;
            return state_type::send_element;
        }

        if (data.winkeyer_enabled || !data.winkeyer_buffer.empty()) {
            return state_type::winkeyer;
        }

        if (data.sent_char_bits > 0 && now_ms >= time_to_decode) {
            auto c = decode_char(data.sent_char_data, data.sent_char_bits);
            data.sent_char_data = data.sent_char_bits = 0;
            hardware->on_char_sent(c);
            if (data.is_recording) {
                add_char_to_recording_memory(c);
            }
        }

        if (hardware->current_ms() > time_for_word_space && data.sent_element != element::none) {
            data.sent_element = element::none;
            hardware->on_char_sent(' ');
            if (data.is_recording) {
                add_char_to_recording_memory(' ');
            }
        }

        return state_type::idle;
    }
}
