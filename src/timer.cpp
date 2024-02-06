//
// Created by benny on 06/02/24.
//

#include "timer.h"

timer::timer() {
    start = std::chrono::high_resolution_clock::now();
}

uint32_t timer::ellapsed_ms() const {
    const auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}
