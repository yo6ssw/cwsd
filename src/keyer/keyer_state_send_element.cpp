#include "keyer_state_send_element.h"
#include "keyer.h"

namespace keyer {

    uint32_t time_to_check_paddles;

    void state_send_element::enter() {
        set_sidetone_and_tx(true);

        time_to_exit_state = hardware->current_ms() + weighted_element_length_ms(data.sent_element);
        time_to_check_paddles = hardware->current_ms() + weighted_element_length_ms(element::dit) * 0.0/50.0f;

        data.sent_char_data <<= 1;
        data.sent_char_bits++;
        if (data.sent_element == element::dit) {
            data.dit_buffered = false;
            hardware->on_dit_on();
        } else {
            data.sent_char_data |= 1;
            data.dah_buffered = false;
            hardware->on_dah_on();
        }
    }

    void state_send_element::exit() {
        data.last_send_time = hardware->current_ms();
        set_sidetone_and_tx(false);
        if (data.sent_element == element::dit) {
            hardware->on_dit_off();
        } else {
            hardware->on_dah_off();
        }
    }

    state_type state_send_element::update(uint32_t now_ms) {
        if (now_ms >= time_to_check_paddles) {
            if (data.sent_element == element::dit) {
                check_dah_paddle();
            } else {
                check_dit_paddle();
            }
        }

        return now_ms >= time_to_exit_state ?
               state_type::inter_element_space :
               state_type::send_element;
    }
}
