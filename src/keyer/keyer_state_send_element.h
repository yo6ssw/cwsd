// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#pragma once

#include "keyer_states.h"

namespace keyer {

    struct state_send_element : public state {
        uint32_t time_to_exit_state{0};

        void enter() override;
        void exit() override;
        state_type update(uint32_t now_ms) override;

        state_type type() override {
            return state_type::send_element;
        }

        static state *instance() {
            static state_send_element instance_;
            return &instance_;
        }
    };
    
}
