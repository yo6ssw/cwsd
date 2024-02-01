#include "keyer_state_autospace.h"
#include "keyer.h"

namespace keyer {

    void state_autospace::enter() {
        auto& profile = get_current_profile();
        time_to_exit_state = hardware->current_ms() + profile.timings.space_between_chars_ms - profile.timings.weighted_inter_element_space_ms;
    }

    state_type state_autospace::update(uint32_t now_ms) {
        return now_ms >= time_to_exit_state ?
               (data.is_playing ? state_type::play : (data.winkeyer_enabled ? state_type::winkeyer
                                                                            : state_type::idle)) :
               state_type::autospace;
    }

}