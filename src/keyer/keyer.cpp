// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include <cstring>
#include <cstdio>
#include <mutex>
#include "keyer.h"
#include "keyer_state_idle.h"
#include "keyer_state_send_element.h"
#include "keyer_state_inter_element_space.h"
#include "keyer_state_autospace.h"
#include "keyer_state_inter_word_space.h"
#include "keyer_state_winkeyer.h"
#include "keyer_state_play.h"
#include "keyer_state_key_down.h"
#include "keyer_state_tune.h"
#include "keyer_state_half_dot_gap.h"

namespace keyer {

    keyer_internal_state data;
    hw_interface *hardware;
    profile profiles[3];
    uint8_t current_profile = 0;

    // Serializes the keyer's shared state across threads. The producer thread feeds the keyer via
    // winkeyer_data()/get_speed() while the dedicated keyer thread consumes it in update(); both
    // touch data.winkeyer_* , the winkeyer_buffer and the active profile's speed/timings. This
    // mutex is non-recursive, so it is locked ONLY at these three entry points — the internal
    // helpers they call (set_speed(), the state machine's update()s, etc.) must run unlocked.
    static std::mutex state_mutex;


    static float audio_level_from_percentage(uint8_t percentage) {
        // https://www.dr-lex.be/info-stuff/volumecontrols.html#ideal3
        double a = 1e-3;
        // Note: in the above mentioned article, b is 6.908. However, exp(6.908) is 1000.24475096
        // and that leads to clipping when multiplied by a = 1e-3, because we'll get a multiplying factor of
        // 1.00024
        double b = 6.9077;
        float volume;
        if (percentage == 0) {
            volume = 0;
        } else {
            volume = a * exp(b * percentage / 100.0f);
        }
        return volume;
    }


    void set_profile(uint8_t id) {
        current_profile = id;
        auto& profile = get_current_profile();
        profile.update_timings();

        data.winkeyer_speed_wpm = profile.get_speed();
    }

    void next_profile() {
        auto next_profile_id = (current_profile + 1) & 1;
        set_profile(next_profile_id);
    }

    uint8_t get_current_profile_id() {
        return current_profile;
    }

    profile& get_current_profile() {
        return profiles[current_profile];
    }

    paddle_state poll_paddles() {
        check_dit_paddle();
        check_dah_paddle();

        uint8_t new_state = 0;
        new_state |= data.dit_buffered ? static_cast<uint8_t>(paddle_state::dit)
                                       : static_cast<uint8_t>(paddle_state::none);
        new_state |= data.dah_buffered ? static_cast<uint8_t>(paddle_state::dah)
                                       : static_cast<uint8_t>(paddle_state::none);
        data.paddles = static_cast<paddle_state>(new_state);
        return data.paddles;
    }

    void check_dit_paddle() {
        if (hardware->is_paddle_pressed(paddle_side::left)) {
            data.dit_buffered = true;
        }
    }

    void check_dah_paddle() {
        if (hardware->is_paddle_pressed(paddle_side::right)) {
            data.dah_buffered = true;
        }
    }

    void set_sidetone_and_tx(bool on) {
        if (on) {
            hardware->tone_on(get_current_profile().sidetone.frequency);
            hardware->on_key_down();
        } else {
            hardware->tone_off();
            hardware->on_key_up();
        }
    }

    uint32_t element_length_ms(element e) {
//        auto wpm = data.winkeyer_enabled ? data.winkeyer_speed_wpm : data.speed_wpm;
//        return e == element::dit ? 1200 / wpm : (1200 * data.dah_dit_ratio) / wpm;

        auto& profile = get_current_profile();
        if (data.winkeyer_enabled) {
            return e == element::dit ? 1200 / data.winkeyer_speed_wpm : (1200 * profile.style.dah_dit_ratio) /
                                                                        data.winkeyer_speed_wpm;
        }
        return e == element::dit ? profile.timings.weighted_dit_ms : profile.timings.weighted_dah_ms;
    }

    uint32_t weighted_element_length_ms(element e) {
        auto& profile = get_current_profile();
        if (data.winkeyer_enabled) {
            return static_cast<uint32_t>(element_length_ms(e) * profile.style.weighting / 50.0f);
        }
        return e == element::dit ? profile.timings.weighted_dit_ms : profile.timings.weighted_dah_ms;
    }

    uint32_t inter_element_space_length_ms() {
        auto& profile = get_current_profile();
        if (data.winkeyer_enabled) {
            return static_cast<uint32_t>(element_length_ms(element::dit) * ((100 - profile.style.weighting) / 50.0f));
        }
        return profile.timings.weighted_inter_element_space_ms;
    }

    static state *states[] = {
            state_idle::instance(),
            state_send_element::instance(),
            state_key_down::instance(),
            state_tune::instance(),
            state_inter_element_space::instance(),
            state_inter_word_space::instance(),
            state_play::instance(),
            state_autospace::instance(),
            state_winkeyer::instance(),
            state_half_dot_gap::instance(),
    };

    void switch_to(state_type next_state) {
        if (data.current_state == nullptr || next_state != data.current_state->type()) {
//            printf("state: %s -> %s\n",
//                   data.current_state == nullptr ? "<null>" : get_state_name(data.current_state->type()),
//                   get_state_name(next_state));
            if (data.current_state != nullptr) {
                data.current_state->exit();
            }
            data.current_state = states[next_state];
            data.current_state->enter();
        }
    }


    // Last 128K sector in STM32F411CE
    const uint32_t MemoryStartAddress = 0x08060000;

    bool persist_memories() {
        return true;
    }

    void init(hw_interface *hw_) {
        hardware = hw_;

        data.winkeyer_speed_wpm = get_current_profile().get_speed();
        data.winkeyer_enabled = false;
        data.winkeyer_waiting_for_arguments = 0;
        data.mode = mode_type::iambic_a;
        data.paddles = paddle_state::none;
        data.sent_element = element::none;
        data.dit_buffered = data.dah_buffered = false;
        data.sent_char_bits = data.sent_char_data = 0;
        data.is_recording = false;
        data.recording_slot = 0;
        data.is_playing = false;
        data.playing_slot = 0;

        load_settings();

        set_profile(0);
        switch_to(state_type::idle);
    }

    void update() {
        std::lock_guard<std::mutex> lock(state_mutex);
        // Drain zero-duration (routing) transitions within a single tick. States such as
        // winkeyer/play only pick the next element and return immediately; switch_to() merely
        // enter()s the next state, so without this loop the new state's update() would not run
        // until the following poll tick. That cost a full poll interval of dead air on every
        // inter-element gap. Timed states (send_element, *_space, autospace) return their own
        // type until their deadline, which breaks the loop; the cap guards against any
        // unexpected routing cycle.
        constexpr int max_transitions_per_tick = 16;
        for (int i = 0; i < max_transitions_per_tick && data.current_state != nullptr; i++) {
            auto next_state = data.current_state->update(hardware->current_ms());
            if (next_state == data.current_state->type()) {
                break;
            }
            switch_to(next_state);
        }
    }

    void set_speed(float wpm) {
        get_current_profile().set_speed(wpm);
    }

    void set_frequency(uint16_t hz) {
        get_current_profile().sidetone.frequency = hz;
    }

    void set_sidetone_wavetype(wave_type type) {
        get_current_profile().sidetone.wave_type = static_cast<uint8_t>(type);
    }

    void set_sidetone_level(uint8_t level) {
        get_current_profile().sidetone.level = level;
    }

    void set_sidetone_ramp_up_ms(uint8_t ms) {
        get_current_profile().sidetone.ramp_up_ms = ms;
    }

    void set_sidetone_ramp_down_ms(uint8_t ms) {
        get_current_profile().sidetone.ramp_down_ms = ms;
    }

    void add_char_to_recording_memory(char c) {
        data.memory[data.recording_slot][data.recording_index++] = c;
        bool space_left = data.recording_index < sizeof(data.memory[data.recording_slot]);
        if (!space_left) {
            stop_recording();
        }
    }

    void start_recording(uint8_t slot) {
        data.is_recording = true;
        data.recording_slot = slot;
        data.recording_index = 0;
        hardware->on_record_started(slot);
    }

    void stop_recording() {
        data.memory[data.recording_slot][data.recording_index] = 0;
        // remove trailing space
        if (data.memory[data.recording_slot][data.recording_index - 1] == ' ') {
            data.memory[data.recording_slot][data.recording_index - 1] = 0;
        }
        data.is_recording = false;
        hardware->on_record_stopped();

//        erase_memories();
        persist_memories();
    }

    void start_playing(uint8_t slot) {
        data.is_playing = true;
        data.playing_slot = slot;
        data.playing_char_index = 0;
        data.playing_bit = 0;
        switch_to(state_type::play);
        hardware->on_play_started(slot);
    }

    void stop_playing() {
        data.is_playing = false;
        hardware->on_play_stopped();
    }

    bool is_recording() {
        return data.is_recording;
    }

    bool is_playing() {
        return data.is_playing;
    }

    const char *get_memory(uint8_t slot) {
        return slot < MemorySlots ? data.memory[slot] : nullptr;
    }

    void set_memory(uint8_t slot, const char *content) {
        if (slot < MemorySlots) {
            uint16_t i = 0;
            while (i < (MemorySlotSize - 1) && *content) {
                data.memory[slot][i++] = *content++;
            }
        }
    }

    uint8_t count_memories() {
        return MemorySlots;
    }

    uint16_t get_memory_max_size() {
        return MemorySlotSize;
    }

    uint16_t get_memory_size(uint8_t slot) {
        return slot < MemorySlots ? strlen(data.memory[slot]) : 0;
    }

    void winkeyer_data(uint8_t c) {
        std::lock_guard<std::mutex> lock(state_mutex);
        // check if we are waiting for pending arguments for immediate commands
        bool execute_pending_command = false;
        if (data.winkeyer_waiting_for_arguments > 0) {
            data.winkeyer_arguments[data.winkeyer_argument_index++] = c;
            data.winkeyer_waiting_for_arguments--;
            if (data.winkeyer_waiting_for_arguments == 0) {
                execute_pending_command = true;
            } else {
                return;
            }
        }

        if (!execute_pending_command && data.winkeyer_remaining_buffered_arguments == 0 && is_winkeyer_command(c) && !is_winkeyer_buffered_command(c)) {
            data.winkeyer_pending_command = c;
            data.winkeyer_waiting_for_arguments = winkey_commands_parameter_count[c];
            data.winkeyer_argument_index = 0;
            if (data.winkeyer_waiting_for_arguments == 0) {
                execute_pending_command = true;
            } else {
                return;
            }
        }

        if (execute_pending_command) {
            // execute
            switch (data.winkeyer_pending_command) {
                case 0x02: // set speed
//                    printf("Setting winkeyer speed to %d from %f",data.winkeyer_arguments[0], data.winkeyer_speed_wpm);
                    data.winkeyer_speed_wpm = data.winkeyer_arguments[0];
                    set_speed(data.winkeyer_arguments[0]);
                    // printf("winkeyer: setting wpm speed to %d\n", data.winkeyer_arguments[0]);
                    hardware->on_parameters_changed();
                    break;
                case 0x03: // weighting
                    printf("setting weighting to %d\n", data.winkeyer_arguments[0]);
                    get_current_profile().set_weighting(data.winkeyer_arguments[0]);
                    break;
                case 0x0A: // clear buffer
                    data.winkeyer_buffer.reset();
                    break;
                case 0x0B: // immediate keying
                    if (data.winkeyer_arguments[0] == 1) {
                        hardware->on_key_down();
                    } else {
                        hardware->on_key_up();
                    }
                    break;
                default:
                    break;
            }
        } else {
            data.winkeyer_buffer.put(c);
            if (data.winkeyer_remaining_buffered_arguments > 0) {
                data.winkeyer_remaining_buffered_arguments --;
            } else {
                if (is_winkeyer_command(c) && is_winkeyer_buffered_command(c)) {
                    data.winkeyer_remaining_buffered_arguments = winkey_commands_parameter_count[c];
                }
            }
        }
    }

    float get_speed() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return get_current_profile().style.character_wpm;
    }

    void set_weighting(uint8_t weight) {
        auto& profile = get_current_profile();
        profile.set_weighting(weight);
    }

    void set_dah_dit_ratio(float ratio) {
        auto& profile = get_current_profile();
        profile.set_dah_dit_ratio(ratio);
    }

    void set_autospace(bool enable) {
        get_current_profile().style.autospace_enabled = enable;
    }

    void set_letterspace(uint8_t percent) {
        auto& profile = get_current_profile();
        profile.set_letterspace(percent);
    }

    void set_keying_compensation(uint8_t ms) {
        auto& profile = get_current_profile();
        profile.set_keying_compensation(ms);
    }

    uint32_t inter_word_space_length_ms() {
        if (data.winkeyer_enabled) {
            return element_length_ms(element::dit) * get_current_profile().style.impulses_between_words;
        }
        return get_current_profile().timings.space_between_words_ms;
    }

    void set_speed_low(float wpm) {
        get_current_profile().style.speed_low = wpm;
        get_current_profile().update_timings();
    }

    void set_speed_high(float wpm) {
        get_current_profile().style.speed_high = wpm;
        get_current_profile().update_timings();
    }

    void set_dah_dit_ratio_hi(float ratio) {
        get_current_profile().style.dah_dit_ratio_high = ratio;
        get_current_profile().update_timings();
    }

    void set_dah_dit_ratio_lo(float ratio) {
        get_current_profile().style.dah_dit_ratio_low = ratio;
        get_current_profile().update_timings();
    }

    void set_weighting_lo(uint8_t weight) {
        get_current_profile().style.weighting_low = weight;
        get_current_profile().update_timings();
    }

    void set_weighting_hi(uint8_t weight) {
        get_current_profile().style.weighting_high = weight;
        get_current_profile().update_timings();
    }

    void set_letterspace_lo(uint8_t percent) {
        get_current_profile().style.extra_letterspace_percent_low = percent;
        get_current_profile().update_timings();
    }

    void set_letterspace_hi(uint8_t percent) {
        get_current_profile().style.extra_letterspace_percent_high = percent;
        get_current_profile().update_timings();
    }

    bool save_profile(const profile& p, uint32_t& address) {
        return true;
    }

    bool save_memory(const char *m, uint32_t& address) {
        return true;
    }

    bool load_profile(profile& p, uint32_t& address) {
        return true;
    }

    bool load_memory(char *m, uint32_t& address) {
        return true;
    }

    bool save_settings() {
        return true;
    }

    bool load_settings() {
        return true;
    }

//    void set_property_changed_callback(property_changed_callback cb) {
//        property_changed_cb = cb;
//    }

    void profile::update_timings() {
        timings = style.calculate_timings();
    }

    void profile::set_speed(float wpm) {
//        printf("setting profile speed to %f\n", wpm);
        style.character_wpm = wpm;
        style.farnsworth_wpm = wpm;
        timings = style.calculate_timings();
    }

    void profile::set_dah_dit_ratio(float ratio) {
        style.dah_dit_ratio = ratio;
        timings = style.calculate_timings();
    }

    void profile::set_weighting(uint8_t weighting) {
        style.weighting = weighting;
        timings = style.calculate_timings();
    }

    void profile::set_letterspace(uint8_t percent) {
        style.extra_letterspace_percent = percent;
        timings = style.calculate_timings();
    }

    void profile::set_keying_compensation(uint8_t ms) {
        style.keying_compensation_ms = ms;
        timings = style.calculate_timings();
    }


    float profile::get_speed() const {
        return style.character_wpm;
    }

    void save_state() {
        // save profiles
        // save memories
    }

    void load_state() {
        // load profiles
        // load memories
    }

    void default_state() {
        // set default profiles
        // set default memories to empty
    }
}
