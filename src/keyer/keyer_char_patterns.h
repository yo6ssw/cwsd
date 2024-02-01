#pragma once
#include <cstdint>

namespace keyer {

    struct char_pattern {
        uint16_t sent_char_data;
        uint8_t sent_char_bits;
        char character;
    };

    char_pattern *get_char_pattern(char chr);
    char decode_char(uint8_t data, uint8_t nr_of_bits);
}