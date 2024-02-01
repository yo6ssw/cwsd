cmake_minimum_required(VERSION 3.27)
project(cwsd)

set(CMAKE_CXX_STANDARD 17)

include_directories("src")
add_executable(cwsd main.cpp
        src/keyer/audio_renderer.h
        src/keyer/audio_renderer.cpp
        src/keyer/keyer.cpp
        src/keyer/keyer.h
        src/keyer/keyer_char_patterns.cpp
        src/keyer/keyer_char_patterns.h
        src/keyer/keyer_state_autospace.cpp
        src/keyer/keyer_state_autospace.h
        src/keyer/keyer_state_idle.cpp
        src/keyer/keyer_state_idle.h
        src/keyer/keyer_state_inter_element_space.cpp
        src/keyer/keyer_state_inter_element_space.h
        src/keyer/keyer_state_inter_word_space.cpp
        src/keyer/keyer_state_inter_word_space.h
        src/keyer/keyer_state_key_down.cpp
        src/keyer/keyer_state_key_down.h
        src/keyer/keyer_state_play.cpp
        src/keyer/keyer_state_play.h
        src/keyer/keyer_state_send_element.cpp
        src/keyer/keyer_state_send_element.h
        src/keyer/keyer_state_tune.cpp
        src/keyer/keyer_state_tune.h
        src/keyer/keyer_state_winkeyer.cpp
        src/keyer/keyer_state_winkeyer.h
        src/keyer/keyer_states.cpp
        src/keyer/keyer_states.h
        src/keyer/morse.cpp
        src/keyer/morse.h
        src/keyer/oscillator.cpp
        src/keyer/oscillator.h
        src/circular_buffer.h
)
