#include "keyer_states.h"

namespace keyer {

    const char *get_state_name(state_type type) {
        switch (type) {
            case idle:
                return "idle";
            case send_element:
                return "send_element";
            case key_down:
                return "key_down";
            case tune:
                return "tune";
            case inter_element_space:
                return "inter_element_space";
            case inter_word_space:
                return "inter_word_space";
            case play:
                return "play";
            case autospace:
                return "autospace";
            case winkeyer:
                return "winkeyer";
            default:
                return "<unknown>";
        }
    }

}