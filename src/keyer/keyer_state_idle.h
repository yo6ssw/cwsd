#pragma once

#include "keyer_states.h"

namespace keyer {
    struct state_idle : public state {

        uint32_t time_for_word_space = 0;

        void enter() override;
        state_type update(uint32_t now_ms) override;

        state_type type() override {
            return state_type::idle;
        }

        static state *instance() {
            static state_idle instance_;
            return &instance_;
        }
    };
}