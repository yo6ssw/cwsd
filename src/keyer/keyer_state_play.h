#pragma once
#include "keyer_states.h"

namespace keyer {

    struct state_play : state {

        bool inter_char_space_played = true;

        state_type update(uint32_t elapsed_ms) override;

        state_type type() override {
            return state_type::play;
        }

        static state *instance() {
            static state_play instance_;
            return &instance_;
        }
    };

}