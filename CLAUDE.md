# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`cwsd` is a CW (Morse) sender daemon for ham radio transceivers (developed against the Icom IC-7300). It runs on Linux, links against **hamlib**, and exposes independent network services so logging/contest software can control the rig and send Morse over the wire, plus an optional Opus-over-UDP audio stream.

## Build / run

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j $(nproc)          # or: cmake --build .  (incremental, from build/)
sudo make install         # installs to /usr/local/bin
```

- Requires CMake ≥ 3.25, a C++17 compiler, hamlib dev headers/libs, and (for the audio stream) ALSA + Opus dev libs (`target_link_libraries(cwsd hamlib Threads::Threads asound opus)`).
- `cmake/GitVersion.cmake` generates `cwsdver-GitVersion.h` from git metadata at configure time; the version embeds the short hash and a `-dirty` flag.
- Run: `./cwsd` (foreground), `./cwsd -d` (daemonize via double-fork), `./cwsd --version`.
- There is **no test suite** and no linter configured.

## Configuration

Config is read from `~/.config/cwsdrc`. Despite the `rc` name it is **YAML**, parsed via the bundled fkYAML header (`src/libs/node.hpp`). See `cwsdrc.sample`. The `rig.model` field is a hamlib rig model number (e.g. `3073` = IC-7300). Each service (`cwdaemon`, `rigctld`) has its own `enabled` flag and TCP/UDP port.

## Architecture

The process is a thin orchestrator (`cwsd` class in `src/cwsd.cpp`) that owns up to two optional servers, each gated by config. `cwsd::run()` is a 2 ms polling loop calling `update()` on each enabled server; a `SIGINT`/`SIGTERM` handler flips the static `is_running` flag to shut down. Each server additionally runs **its own worker thread**, so `update()` on the main thread is light.

### rigctld_server (`src/rigctld_server.*`)
A partial reimplementation of hamlib's **rigctld TCP protocol** (default port 4532) so clients like WSJT-X/fldigi can query and set frequency, mode, PTT, VFO, etc.
- Owns the hamlib `RIG*` handle (`rig_init` + `rig_open` using the configured model and serial device path) and serves multiple clients with a single `poll()`-based non-blocking event loop in `work()`.
- Disconnect detection is a **hack**: a `rig_set_debug_callback` matches the string `"read failed"` to mark the rig disconnected, then the loop drops clients and attempts to reopen. hamlib open/close/cleanup must all happen on the worker thread.
- `interpret_command()` is the protocol command dispatcher (single-letter commands like `f`/`F`, `m`/`M`, `t`/`T`, `q`/`Q` quit, plus `\`-prefixed extended commands). `dump_state()` is a near 1:1 port of a specific hamlib commit and contains hardcoded values noted with `TODO`s.

### cwdaemon_server (`src/cwdaemon_server.*`)
Implements the **cwdaemon UDP protocol** (default port 6789). `client_worker()` (its own thread) receives datagrams: ESC-prefixed control commands (`ESC 5` = exit, `ESC 4` = abort) and plaintext to key as Morse.
- Messages are translated into **WinKeyer byte commands** by `cw_daemon::to_winkeyer()` (`src/cw_daemon.cpp`), which handles inline speed changes (`+`/`-`), gaps (`~`), and ESC commands (`ESC 2` set speed, `ESC c` tune for N seconds, `ESC u`/`ESC d` speed delta, etc.), then fed byte-by-byte into the keyer via `keyer::winkeyer_data()`.
- `key_interface` (implements `keyer::hw_interface`) drives the rig's serial control lines over the same device path: **DTR = key, RTS = PTT**, toggled via `ioctl(TIOCMBIS/TIOCMBIC)`. It polls `isatty(fd)` to detect USB serial connect/disconnect and reopens.

### audio_stream_server (`src/audio_stream_server.*`)
Optional, third service (config section `audio`, disabled by default). Captures rig audio from an **ALSA** device, encodes it with **Opus** (links `asound` + `opus`), and fans it out as UDP datagrams to every subscribed client. There is **no configured target** — only a `port` to bind; clients subscribe (and NAT-punch) by sending any datagram to that port.
- Like the other servers it spawns a worker thread in its constructor and joins it in `stop()`; `update()` is a no-op, so the streaming cadence is never coupled to the 2 ms main loop. The encoder and bound UDP socket are set up in the constructor (a bind failure throws); ALSA capture is (re)opened on the worker thread so a missing device never blocks startup.
- `work()` reads `frame_ms` worth of S16LE frames via `snd_pcm_readi` (recovering from `-EPIPE` overruns with `snd_pcm_prepare`), Opus-encodes each frame, and `broadcast()`s it to all clients. Device disconnect is handled like `key_interface`: on read failure it closes and retries `open_capture()`.
- **Client tracking** (`poll_clients()`, called once per frame): the bound socket is **non-blocking**; each pending datagram registers or refreshes its sender in the `clients` map (keyed by `addr<<16 | port`), ignoring the payload. Clients silent longer than `client_timeout_ms` are dropped, so subscribers must keep sending a small periodic keepalive. The whole map lives on the worker thread (recv, expire, send), so no locking is needed.
- **Wire format** (server→client): a 4-byte big-endian sequence number followed by the raw Opus packet; the same sequence stream goes to every client. `sample_rate` must be a valid Opus rate (8/12/16/24/48 kHz) and `frame_ms` a valid Opus frame size.

### keyer engine (`src/keyer/`)
The Morse heart of the daemon — a WinKeyer-compatible keyer written in an embedded-device style: a `keyer` namespace with a single global `data` struct (`keyer_internal_state`) and a `hw_interface` abstraction so the same engine could run on microcontroller or daemon. It is a **state machine**: states enumerated in `keyer_states.h` (`idle`, `send_element`, `key_down`, `tune`, `inter_element_space`, `inter_word_space`, `play`, `autospace`, `winkeyer`, `half_dot_gap`), each a class in its own `keyer_state_*.cpp`. `keyer::update()` ticks the current state. Supporting pieces: `circular_buffer.h`, profiles (speed/weighting/ratio), `morse.*` char patterns, and `oscillator`/`audio_renderer` for sidetone generation.

### Cross-cutting
- **Logging**: easylogging++ (`src/libs/easylogging++.*`), configured in `main.cpp` with hierarchical levels and size-based log rotation (`pre_rollout_callback` renames `.log.1`..`.log.9`). `INITIALIZE_EASYLOGGINGPP` lives in `main.cpp`.
- `src/events.*` defines an event bus that is currently stubbed (`set_event_bus` is a no-op).
- `shared/` holds deployment artifacts: a udev rule (`80-ic7300.rules`) that creates a stable `/dev/icom7300` symlink, and a systemd unit (`cwsd.service`).

## Conventions worth knowing

- The servers each spawn a thread in their constructor and join it in `stop()`. hamlib calls (rigctld), serial ioctls (cwdaemon), and ALSA/Opus calls (audio_stream) are confined to their respective worker threads.
- Several values are deliberately hardcoded with `TODO` markers (notably in `rigctld_server::dump_state()` and `chk_vfo`); match the existing style rather than over-engineering when extending protocol coverage.
