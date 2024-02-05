#pragma once

#include <cstdint>
#include "keyer_states.h"
#include "circular_buffer.h"
#include "morse.h"

namespace keyer {

    enum class paddle_side {
        left,
        right
    };

    enum class paddle_state : uint8_t {
        none = 0,
        dit = 1,
        dah = 2,
        both = 3
    };

    enum class mode_type {
        straight,
        iambic_a,
        iambic_b,
        tune
    };


    const uint8_t MemorySlots = 4;
    const uint16_t MemorySlotSize = 256;


    struct sidetone_settings {
        uint16_t frequency = 500;
        uint8_t wave_type = 0;
        uint8_t level = 70;
        uint8_t ramp_up_ms = 3;
        uint8_t ramp_down_ms = 3;
    };

    struct ptt_settings {
        float lead_ms;
        float tail_ms;
        uint8_t hang_type; // 0 => no hang, 1 => wordspace + 1 dit, 2 => wordpsace + 2 dits, 3 => wordspace + 3 dits, 4 => wordspace + 4 dits
        uint8_t reserved[3];
    };

    struct profile {
        code_style style{};
        code_timings timings{};
        sidetone_settings sidetone{};
        ptt_settings ptt{};

        void update_timings();
        void set_speed(float wpm);
        void set_dah_dit_ratio(float ratio);
        void set_weighting(uint8_t weighting);
        void set_letterspace(uint8_t percent);
        void set_keying_compensation(uint8_t ms);
        float get_speed() const;
    };

    void set_profile(uint8_t id);
    void next_profile();
    uint8_t get_current_profile_id();
    profile& get_current_profile();


    extern struct keyer_internal_state {
        state *current_state;
        mode_type mode;
        paddle_state paddles;
        element sent_element;
        bool dit_buffered;
        bool dah_buffered;
        uint8_t sent_char_data;
        uint8_t sent_char_bits;

        bool is_recording;
        uint8_t recording_slot;
        uint16_t recording_index;
        bool is_playing;
        uint8_t playing_slot;

        char memory[MemorySlots][MemorySlotSize] = {0};

        uint16_t playing_char_index;
        uint16_t playing_data;
        uint8_t playing_bit;

        circular_buffer<uint8_t> winkeyer_buffer{160};
        bool winkeyer_enabled;
        uint8_t winkeyer_pending_command;
        uint8_t winkeyer_waiting_for_arguments;
        uint8_t winkeyer_arguments[15];
        uint8_t winkeyer_argument_index;
        float winkeyer_speed_wpm;
        uint32_t last_send_time;
    } data;

    struct hw_interface {
        virtual void tone_on(uint16_t hz) {}
        virtual void tone_off() {}
        virtual uint32_t current_ms() = 0;
        virtual bool is_paddle_pressed(paddle_side side) = 0;

        virtual void on_dit_on() {};

        virtual void on_dit_off() {};

        virtual void on_dah_on() {};

        virtual void on_dah_off() {};

        virtual void on_key_down() {};

        virtual void on_key_up() {};

        virtual void on_char_sent(char c) {};

        virtual void on_play_started(uint8_t slot) {};

        virtual void on_play_stopped() {};

        virtual void on_record_started(uint8_t slot) {};

        virtual void on_record_stopped() {};

        virtual void on_parameters_changed() {};

    };

    extern hw_interface *hardware;

    void init(hw_interface *hw);
//    void set_property_changed_callback(property_changed_callback cb);
    void update();
    void audio_tick(int16_t& left, int16_t& right);

    bool save_settings();
    bool load_settings();

    void set_speed_low(float wpm);
    void set_speed_high(float wpm);

    void set_dah_dit_ratio(float ratio);
    void set_dah_dit_ratio_hi(float ratio);
    void set_dah_dit_ratio_lo(float ratio);

    /**
     * from 10 to 90, percentage
     * @param weight
     */
    void set_weighting(uint8_t weight);
    void set_weighting_lo(uint8_t weight);
    void set_weighting_hi(uint8_t weight);

    /**
     * from 10 to 90, percentage
     * @param extra letter space
     */
    void set_letterspace(uint8_t percent);
    void set_letterspace_lo(uint8_t percent);
    void set_letterspace_hi(uint8_t percent);

    void set_speed(float wpm);
    void set_frequency(uint16_t hz);
    void set_sidetone_wavetype(wave_type type);
    void set_sidetone_level(uint8_t level); // 0 .. 100
    void set_sidetone_ramp_up_ms(uint8_t ms);
    void set_sidetone_ramp_down_ms(uint8_t ms);


    void set_autospace(bool enable);
    void set_keying_compensation(uint8_t ms);

    void start_recording(uint8_t slot);
    void stop_recording();
    void start_playing(uint8_t slot);
    void stop_playing();
    bool is_recording();
    bool is_playing();

    /**
     * returns number of memory slots
     * @return
     */
    uint8_t count_memories();

    /**
     * returns the maximum size a memory slot can have
     * @return
     */
    uint16_t get_memory_max_size();

    /**
     * returns the current size of the contents stored in the memory with specified slot
     * @param slot
     * @return the memory slot size or 0 if invalid slot or memory empty
     */
    uint16_t get_memory_size(uint8_t slot);
    /**
     * returns the specified memory slot contents or nullptr if invalid slot
     * @param slot
     * @return
     */
    const char *get_memory(uint8_t slot);
    /**
     * sets the memory slot with the specified content. data will be copied
     * @param slot
     * @param data
     */
    void set_memory(uint8_t slot, const char *data);
    void winkeyer_data(uint8_t c);
    float get_speed();


    paddle_state poll_paddles();
    void check_dit_paddle();
    void check_dah_paddle();

    void add_char_to_recording_memory(char c);
    uint32_t element_length_ms(element e);
    uint32_t weighted_element_length_ms(element e);
    void set_sidetone_and_tx(bool on);
    uint32_t inter_element_space_length_ms();
    uint32_t inter_word_space_length_ms();
    char decode_char(uint8_t data, uint8_t nr_of_bits);

}
