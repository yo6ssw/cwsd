cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GitVersion)

project(cwsd LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories("src")
add_executable(cwsd main.cpp
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
        src/keyer/keyer_state_half_dot_gap.cpp
        src/keyer/keyer_state_half_dot_gap.h
        src/cw_daemon.cpp
        src/cw_daemon.h
        src/udp_server.cpp
        src/udp_server.h
        src/timer.cpp
        src/timer.h
        src/libs/easylogging++.cc
        src/libs/easylogging++.h
        src/libs/node.hpp
        src/rigctld_server.cpp
        src/rigctld_server.h
        src/cwdaemon_server.cpp
        src/cwdaemon_server.h
        src/audio_stream_server.cpp
        src/audio_stream_server.h
        src/events.cpp
        src/events.h
        src/cwsd.cpp
        src/cwsd.h
)
target_link_libraries(cwsd hamlib Threads::Threads asound opus)

# easylogging++ is not thread-safe unless told so. cwsd logs from several worker
# threads (keyer, audio, rigctld), so without this their output interleaves into
# garbled/concatenated lines (e.g. "...priority 11audio streaming...").
target_compile_definitions(cwsd PRIVATE ELPP_THREAD_SAFE)

add_version_info(TARGET cwsd PREFIX cwsdver)

# add_version_info() captures the git hash at *configure* time. Re-run CMake (and
# thus regenerate the version header) whenever HEAD moves, so a plain rebuild
# after a commit/pull/checkout reports the right hash in `cwsd --version`.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
    set(_git_version_deps "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD")
        list(APPEND _git_version_deps "${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_git_version_deps})
endif()

install(TARGETS cwsd DESTINATION /usr/local/bin)
