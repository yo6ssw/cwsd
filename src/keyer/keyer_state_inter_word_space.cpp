#include "keyer_state_inter_word_space.h"
#include "keyer.h"

namespace keyer {

    void state_inter_word_space::enter() {
        time_to_exit = data.last_send_time + inter_word_space_length_ms();
        hardware->on_char_sent(' ');
    }

    state_type state_inter_word_space::update(uint32_t elapsed_ms) {
        poll_paddles();
        return hardware->current_ms() >= time_to_exit ?
               (data.winkeyer_enabled ? state_type::winkeyer : state_type::play) :
               state_type::inter_word_space;
    }

}