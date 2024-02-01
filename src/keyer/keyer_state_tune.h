#pragma once

#include "keyer_states.h"

namespace keyer {

    struct state_tune : state {
        state_type update(uint32_t elapsed_ms) override {
            return state_type::idle;
        }

        state_type type() override {
            return state_type::tune;
        }

        static state *instance() {
            static state_tune instance_;
            return &instance_;
        }
    };

}