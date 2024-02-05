#include "keyer_state_winkeyer.h"
#include "keyer.h"

#include <cstdio>

#include "keyer_char_patterns.h"

namespace keyer {
    const uint8_t winkey_commands_parameter_count[] = {
        1, // 0x00, admin command
        1, // 0x01 sidetone control
        1, // 0x02 set wpm speed
        1, // 0x03 set weighting
        2, // 0x04 set ptt lead/tail
        3, // 0x05 setup speed pot
        1, // 0x06 set pause state
        0, // 0x07 return current pot setting
        0, // 0x08 backup input buffer pointer by one char
        1, // 0x09 set pin config
        0, // 0x0A clear buffer
        1, // 0x0B key immediate
        1, // 0x0C set HSCW
        1, // 0x0D set FARNSWORTH WPM
        1, // 0x0E set WinKeyer mode
        15, // 0x0F load defaults
        1, // 0x10 set first extension
        1, // 0x11 set key compensation
        1, // 0x12 set paddle switchpoint
        0, // 0x13 null command
        1, // 0x14 sw paddle
        0, // 0x15 return WinKeyer status byte
        1, // 0x16 pointer command
        1, // 0x17 set dit/dah ratio
        // ---- buffered commands -----
        1, // 0x18 ptt on/off
        1, // 0x19 key buffered
        1, // 0x1A wait for nn seconds
        2, // 0x1B merge letters
        1, // 0x1C buffered speed change
        1, // 0x1D buffered HSCW speed change
        0, // 0x1E cancel buffered speed change
        0, // 0x1F buffered NOP
    };

    bool is_winkeyer_command(uint8_t c) {
        return c <= 0x1F;
    }

    bool is_winkeyer_buffered_command(uint8_t c) {
        return c <= 0x1F && c >= 0x18;
    }

    void state_winkeyer::enter() {
        data.winkeyer_enabled = true;
    }

    state_type state_winkeyer::update(uint32_t elapsed_ms) {
        poll_paddles();
        if (data.paddles != paddle_state::none) {
            data.winkeyer_enabled = false;
            data.winkeyer_buffer.reset();
            return state_type::idle;
        }

        if (data.playing_bit == 0) {
            if (!inter_char_space_played) {
                inter_char_space_played = true;
                return state_type::autospace;
            }

            if (data.winkeyer_buffer.empty())
                return state_type::winkeyer;

            if (data.winkeyer_buffer.empty()) {
                return state_type::winkeyer;
            }

            // check if next byte is command and we have all needed parameters
            // in case we do not have what's needed, wait some more
            auto c = data.winkeyer_buffer.peek();
            if (is_winkeyer_command(c) && data.winkeyer_buffer.size() <= winkey_commands_parameter_count[c]) {
                return state_type::winkeyer;
            }

            // consume the read byte
            data.winkeyer_buffer.get();
            if (is_winkeyer_command(c)) {
                // get command arguments
                static uint8_t arguments[15] = {0};
                for (uint8_t i = 0; i < winkey_commands_parameter_count[c]; i++) {
                    arguments[i] = data.winkeyer_buffer.get();
                }
                // interpret command
                switch (c) {
                    case 0x1C: // buffered speed change
                        printf("Buffered speed change to %d\n", arguments[0]);
                        data.winkeyer_speed_wpm = arguments[0];
                        break;
                    case 0x0A:
                        data.winkeyer_buffer.reset();
                        break;
                    default:
                        break;
                }
                return state_type::winkeyer;
            } else {
                if (c == '|') {
                    return state_type::half_dot_gap;
                } else {
                    auto pattern = get_char_pattern(c);
                    if (pattern == nullptr) {
//                        printf("!!! Pattern not found for char [%c]\n", c);
                        return state_type::inter_word_space;
                    } else {
//                        printf("!!! Sending char [%c]\n", c);
                        data.playing_data = pattern->sent_char_data;
                        data.playing_bit = pattern->sent_char_bits;
                        inter_char_space_played = false;
                    }
                }
            }
        }

        auto mask = 1 << (data.playing_bit - 1);
        data.playing_bit--;
        data.sent_element = ((data.playing_data & mask) > 0) ? element::dah : element::dit;
        return state_type::send_element;
    }
}
