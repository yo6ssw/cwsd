#pragma once

#include <cstdint>
#include "oscillator.h"


namespace keyer {

    enum class element {
        none,
        dit,
        dah
    };

    enum state_type {
        idle = 0,
        send_element = 1,
        key_down = 2,
        tune = 3,
        inter_element_space = 4,
        inter_word_space = 5,
        play = 6,
        autospace = 7,
        winkeyer = 8,
    };

    struct state {
        virtual void enter() {};

        virtual void exit() {};
        virtual state_type update(uint32_t now_ms) = 0;
        virtual state_type type() = 0;
    };

    const char *get_state_name(state_type type);
}