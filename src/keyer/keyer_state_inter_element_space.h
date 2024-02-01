#pragma once

#include "keyer_states.h"

namespace keyer {

    struct state_inter_element_space : public state {
        uint32_t time_to_exit_state{0};

        void enter() override;
        state_type update(uint32_t now_ms) override;

        state_type type() override {
            return state_type::inter_element_space;
        }

        static state *instance() {
            static state_inter_element_space instance_;
            return &instance_;
        }
    };

}