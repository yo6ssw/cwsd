// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#pragma once

#include "keyer_states.h"

namespace keyer {

struct state_tune : state {
  state_type update(uint32_t elapsed_ms) override { return state_type::idle; }

  state_type type() override { return state_type::tune; }

  static state* instance() {
    static state_tune instance_;
    return &instance_;
  }
};

}  // namespace keyer