// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#ifndef CWSD_AUDIO_STREAM_SERVER_H
#define CWSD_AUDIO_STREAM_SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <netinet/in.h>
#include "timer.h"

struct audio_stream_config {
    bool enabled = false;
    std::string device = "default";   // ALSA capture device (e.g. plughw:CARD=CODEC,DEV=0)
    uint16_t port = 0;                // UDP port to bind; clients subscribe by sending here
    uint32_t sample_rate = 8000;      // valid opus rate (8/12/16/24/48 kHz); 8 kHz
                                      // (4 kHz Nyquist) covers CW audio at minimal
                                      // bandwidth + CPU
    int channels = 1;
    int bitrate = 96000;              // opus target bitrate in bits/s. Low rates spread
                                      // quantization noise that a multi-channel CW
                                      // decoder picks up as spurious signals on
                                      // strong/noisy audio; a high rate keeps it clean.
    int frame_ms = 20;                // opus frame size in ms (2.5/5/10/20/40/60)
    int client_timeout_ms = 10000;    // drop clients that have been silent this long
    int fec_loss_perc = 10;           // expected packet loss %, drives Opus in-band FEC
                                      // (LBRR): the encoder embeds a low-bitrate copy of
                                      // the previous frame so a client can recover a
                                      // single lost packet from the next one. 0 disables.
};

// Forward declarations so the alsa/opus headers stay confined to the .cpp.
typedef struct _snd_pcm snd_pcm_t;
struct OpusEncoder;

// Captures audio from an ALSA device, encodes it with Opus and fans it out as UDP
// datagrams to every subscribed client. There is no configured target: clients
// subscribe by sending any datagram to the bound port and stay subscribed by
// continuing to send (a periodic keepalive); silent clients are dropped. Like the
// other servers it runs its own worker thread; update() on the main loop is a no-op,
// so the streaming cadence is never coupled to the main loop.
class audio_stream_server {
public:
    explicit audio_stream_server(audio_stream_config cfg);
    ~audio_stream_server();

    void update();
    void stop();

private:
    struct client {
        sockaddr_in addr;
        uint32_t last_seen_ms;
    };

    void work();
    bool open_capture();
    void close_capture();
    bool open_socket();
    void poll_clients();               // register inbound senders, expire stale ones
    void broadcast(const uint8_t *payload, size_t len);

    audio_stream_config config;
    snd_pcm_t *pcm = nullptr;
    OpusEncoder *encoder = nullptr;
    int sockfd = -1;
    std::unordered_map<uint64_t, client> clients;   // keyed by (addr << 16 | port)
    unsigned int frame_size = 0;       // samples per channel per opus frame
    uint32_t sequence = 0;
    bool capture_was_open = false;
    timer clock;

    std::thread worker_thread;
    std::atomic<bool> is_running{false};
};

#endif //CWSD_AUDIO_STREAM_SERVER_H
