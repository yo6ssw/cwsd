#pragma once

#include "keyer_states.h"

namespace keyer {

    extern const uint8_t winkey_commands_parameter_count[];
    bool is_winkeyer_command(uint8_t c);
    bool is_winkeyer_buffered_command(uint8_t c);

    struct state_winkeyer : state {

        bool inter_char_space_played = true;

        void enter() override;
        state_type update(uint32_t elapsed_ms) override;

        state_type type() override {
            return state_type::winkeyer;
        }

        static state *instance() {
            static state_winkeyer instance_;
            return &instance_;
        }
    };

}