#pragma once
#include "keyer_states.h"

namespace  keyer {

    struct state_inter_word_space : state {
        uint32_t time_to_exit = 0;

        void enter() override;
        state_type update(uint32_t elapsed_ms) override;

        state_type type() override {
            return state_type::inter_word_space;
        }

        static state *instance() {
            static state_inter_word_space instance_;
            return &instance_;
        }
    };

}