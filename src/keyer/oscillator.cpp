#include "oscillator.h"
#include <cmath>

oscillator::oscillator(float sr) : samplerate(sr) {
    // sine
    for (int i = 0; i < 1024; i++) {
        tables[0][i] = static_cast<int16_t>(sin(i * 2.0 * M_PI / 1024.0f) * 32767.0);
    }

    // triangle
    for (int16_t i = 0; i < 1024; i++) {
        if (i < 512) {
            tables[static_cast<uint8_t>(wave_type::triangle)][(i + 256) & 0b1111111111] = (i - 256) * 32767 / 256;
        } else {
            tables[static_cast<uint8_t>(wave_type::triangle)][(i + 256) & 0b1111111111] =
                    (255 - (i - 512)) * 32767 / 256;
        }
    }

    // square
    for (int16_t i = 0; i < 1024; i++) {
        tables[static_cast<uint8_t>(wave_type::square)][i] = i < 512 ? -32767 : 32767;
    }

    // sawtooth
    for (int16_t i = 0; i < 1024; i++) {
        tables[static_cast<uint8_t>(wave_type::sawtooth)][(i + 512) & 0b1111111111] = (i * 65535 / 1025) - 32767;
    }
}

void oscillator::set_wave_type(wave_type type_) {
    type = type_;
    table_index = static_cast<uint8_t>(type);
}

void oscillator::set_frequency(float frequency) {
    phase_add = static_cast<uint32_t>(std::numeric_limits<uint32_t>::max() * (frequency / samplerate));
//    phase_add = 49320000;
}

void oscillator::reset() {
    phase = 0;
}

int16_t oscillator::tick() {
    auto idx = phase >> 22;
    phase += phase_add;
    return tables[table_index][idx];
}

int16_t oscillator::get_at_phase_offset(uint32_t phase_offset) {
    return tables[table_index][(phase+phase_offset) >> 22];
}
