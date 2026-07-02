cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GitVersion)

project(cwsd LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Optionally link a bundled *static* hamlib instead of the system shared library.
# This yields a portable binary that carries a known-good hamlib, sidestepping both
# hamlib's silent 4.x ABI breaks and distros shipping an old hamlib. Used for the
# release tarball artifacts (see scripts/build-hamlib-static.sh + the CI workflow);
# distro-native packages keep the default dynamic linking.
option(CWSD_STATIC_HAMLIB "Link a bundled static libhamlib.a (portable builds)" OFF)
set(HAMLIB_STATIC_ROOT "" CACHE PATH
    "Install prefix of a statically-built hamlib (required when CWSD_STATIC_HAMLIB=ON)")

# Fully static (musl/Alpine): link libc and every dependency statically for a
# single dependency-free binary that runs on any Linux. Implies CWSD_STATIC_HAMLIB;
# expects static libopus/libasound (and hamlib) under HAMLIB_STATIC_ROOT plus a
# static libusb on the link path (see scripts/build-static-deps.sh + the CI job).
# Only works with a musl toolchain — a fully-static glibc breaks NSS/getaddrinfo.
option(CWSD_FULLY_STATIC "Fully static musl binary: link libc and all deps statically" OFF)
if(CWSD_FULLY_STATIC)
    set(CWSD_STATIC_HAMLIB ON)
endif()

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
        src/remote_key_server.cpp
        src/remote_key_server.h
        src/remote_key_protocol.h
        src/events.cpp
        src/events.h
        src/cwsd.cpp
        src/cwsd.h
)
if(CWSD_STATIC_HAMLIB)
    if(NOT HAMLIB_STATIC_ROOT OR NOT EXISTS "${HAMLIB_STATIC_ROOT}/lib/libhamlib.a")
        message(FATAL_ERROR
            "CWSD_STATIC_HAMLIB=ON requires -DHAMLIB_STATIC_ROOT=<prefix> containing "
            "lib/libhamlib.a — build it with scripts/build-hamlib-static.sh")
    endif()
    message(STATUS "cwsd: linking static hamlib from ${HAMLIB_STATIC_ROOT}")
    target_include_directories(cwsd PRIVATE "${HAMLIB_STATIC_ROOT}/include")
    # Link the static archive by full path, then its private deps. hamlib is built
    # --without-readline, so the only transitive libs are libusb-1.0, libm and libdl,
    # kept dynamic (ABI-stable, present on every Linux). glibc stays dynamic too — a
    # fully-static glibc breaks NSS/getaddrinfo, which hamlib's network rigs need.
    target_link_libraries(cwsd "${HAMLIB_STATIC_ROOT}/lib/libhamlib.a" usb-1.0 m ${CMAKE_DL_LIBS})
else()
    target_link_libraries(cwsd hamlib)
endif()
target_link_libraries(cwsd Threads::Threads asound opus)

if(CWSD_FULLY_STATIC)
    # Prefer the static libopus/libasound (and hamlib) under the prefix, and link
    # everything — including musl libc — statically.
    target_include_directories(cwsd PRIVATE "${HAMLIB_STATIC_ROOT}/include")
    target_link_directories(cwsd PRIVATE "${HAMLIB_STATIC_ROOT}/lib")
    target_link_options(cwsd PRIVATE -static)
endif()

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

include(GNUInstallDirs)
install(TARGETS cwsd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The system service reads ${sysconfdir}/cwsd/cwsdrc — ship the sample there so
# the admin just copies it to cwsdrc (with prefix=/usr, sysconfdir is /etc).
# Absolute (FULL) sysconfdir so it matches the unit's --config path: GNUInstallDirs
# maps this to /etc/cwsd for prefix=/usr (the relative var would give /usr/etc).
install(FILES cwsdrc.sample DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/cwsd)

# systemd unit + udev rule land under <prefix>/lib/... — correct for package
# builds (prefix=/usr). Toggle off for a bare binary-only install.
option(CWSD_INSTALL_SYSTEM_FILES "Install the systemd unit and udev rule" ON)
if(CWSD_INSTALL_SYSTEM_FILES)
    # Fill @CMAKE_INSTALL_FULL_BINDIR@/@CMAKE_INSTALL_FULL_SYSCONFDIR@ so the unit's
    # ExecStart matches wherever this prefix installs (no hardcoded /usr/local/bin).
    configure_file(shared/cwsd.service.in
                   "${CMAKE_CURRENT_BINARY_DIR}/cwsd.service" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cwsd.service"
            DESTINATION lib/systemd/system)
    install(FILES shared/80-ic7300.rules DESTINATION lib/udev/rules.d)
endif()
