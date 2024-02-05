#pragma once

#include "keyer_states.h"

namespace keyer {

    struct state_half_dot_gap : public state {
        uint32_t time_to_exit_state{0};

        void enter() override;
        state_type update(uint32_t now_ms) override;

        state_type type() override {
            return state_type::half_dot_gap;
        }

        static state *instance() {
            static state_half_dot_gap instance_;
            return &instance_;
        }
    };

}