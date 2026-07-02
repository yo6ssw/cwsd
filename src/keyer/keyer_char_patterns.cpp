// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "keyer_char_patterns.h"
namespace keyer {

    const char_pattern char_table[] = {
            {.sent_char_data = 0b01, .sent_char_bits = 2, .character = 'A'},
            {.sent_char_data = 0b1000, .sent_char_bits = 4, .character = 'B'},
            {.sent_char_data = 0b1010, .sent_char_bits = 4, .character = 'C'},
            {.sent_char_data = 0b100, .sent_char_bits = 3, .character = 'D'},
            {.sent_char_data = 0b0, .sent_char_bits = 1, .character = 'E'},
            {.sent_char_data = 0b0010, .sent_char_bits = 4, .character = 'F'},
            {.sent_char_data = 0b110, .sent_char_bits = 3, .character = 'G'},
            {.sent_char_data = 0b0000, .sent_char_bits = 4, .character = 'H'},
            {.sent_char_data = 0b00, .sent_char_bits = 2, .character = 'I'},
            {.sent_char_data = 0b0111, .sent_char_bits = 4, .character = 'J'},
            {.sent_char_data = 0b101, .sent_char_bits = 3, .character = 'K'},
            {.sent_char_data = 0b0100, .sent_char_bits = 4, .character = 'L'},
            {.sent_char_data = 0b11, .sent_char_bits = 2, .character = 'M'},
            {.sent_char_data = 0b10, .sent_char_bits = 2, .character = 'N'},
            {.sent_char_data = 0b111, .sent_char_bits = 3, .character = 'O'},
            {.sent_char_data = 0b0110, .sent_char_bits = 4, .character = 'P'},
            {.sent_char_data = 0b1101, .sent_char_bits = 4, .character = 'Q'},
            {.sent_char_data = 0b010, .sent_char_bits = 3, .character = 'R'},
            {.sent_char_data = 0b000, .sent_char_bits = 3, .character = 'S'},
            {.sent_char_data = 0b1, .sent_char_bits = 1, .character = 'T'},
            {.sent_char_data = 0b001, .sent_char_bits = 3, .character = 'U'},
            {.sent_char_data = 0b0001, .sent_char_bits = 4, .character = 'V'},
            {.sent_char_data = 0b011, .sent_char_bits = 3, .character = 'W'},
            {.sent_char_data = 0b1001, .sent_char_bits = 4, .character = 'X'},
            {.sent_char_data = 0b1011, .sent_char_bits = 4, .character = 'Y'},
            {.sent_char_data = 0b1100, .sent_char_bits = 4, .character = 'Z'},
            {.sent_char_data = 0b11111, .sent_char_bits = 5, .character = '0'},
            {.sent_char_data = 0b01111, .sent_char_bits = 5, .character = '1'},
            {.sent_char_data = 0b00111, .sent_char_bits = 5, .character = '2'},
            {.sent_char_data = 0b00011, .sent_char_bits = 5, .character = '3'},
            {.sent_char_data = 0b00001, .sent_char_bits = 5, .character = '4'},
            {.sent_char_data = 0b00000, .sent_char_bits = 5, .character = '5'},
            {.sent_char_data = 0b10000, .sent_char_bits = 5, .character = '6'},
            {.sent_char_data = 0b11000, .sent_char_bits = 5, .character = '7'},
            {.sent_char_data = 0b11100, .sent_char_bits = 5, .character = '8'},
            {.sent_char_data = 0b11110, .sent_char_bits = 5, .character = '9'},
            {.sent_char_data = 0b001100, .sent_char_bits = 6, .character = '?'},
            {.sent_char_data = 0b10010, .sent_char_bits = 5, .character = '/'},
            {.sent_char_data = 0b110011, .sent_char_bits = 6, .character = ','},
            {.sent_char_data = 0b010101, .sent_char_bits = 6, .character = '.'},
            {.sent_char_data = 0b10001, .sent_char_bits = 5, .character = '='},
            {.sent_char_data = 0b100001, .sent_char_bits = 6, .character = '-'},
            {.sent_char_data = 0b011110, .sent_char_bits = 6, .character = '"'},
            {.sent_char_data = 0b10110, .sent_char_bits = 5, .character = '('},
            {.sent_char_data = 0b101101, .sent_char_bits = 6, .character = ')'},
            {.sent_char_data = 0b000101, .sent_char_bits = 6, .character = '$'}, // SK
            {.sent_char_data = 0b01010, .sent_char_bits = 5, .character = '+'}, // AR
            {.sent_char_data = 0b1000101, .sent_char_bits = 7, .character = '['}, // BK
            {.sent_char_data = 0b01000, .sent_char_bits = 5, .character = '{'}, // AS
            {.sent_char_data = 0b10110, .sent_char_bits = 5, .character = '}'}, // KN
    };

    char_pattern *get_char_pattern(char chr) {
        for (auto& c: char_table) {
            if (c.character == chr) {
                return const_cast<char_pattern *>(&c);
            }
        }
        return nullptr;
    }

    char decode_char(uint8_t data, uint8_t nr_of_bits) {
        for (auto c: char_table) {
            if (c.sent_char_data == data && c.sent_char_bits == nr_of_bits) {
                return c.character;
            }
        }
        return '?';
    }

}