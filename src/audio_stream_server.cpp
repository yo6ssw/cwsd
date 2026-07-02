// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "audio_stream_server.h"

#include <alsa/asoundlib.h>
#include <opus/opus.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <stdexcept>
#include "libs/easylogging++.h"

// Wire format (server -> client): a 4-byte big-endian sequence number followed by
// the raw Opus packet. The receiver uses the sequence to detect loss / reorder.
// Subscription (client -> server): any datagram sent to the bound port registers or
// refreshes the sender; its contents are ignored. Clients must keep sending (a small
// periodic keepalive) or they are dropped after config.client_timeout_ms.
namespace {
    constexpr size_t HEADER_BYTES = 4;
    constexpr size_t MAX_PACKET = 1500;

    void put_be32(uint8_t *p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v >> 24);
        p[1] = static_cast<uint8_t>(v >> 16);
        p[2] = static_cast<uint8_t>(v >> 8);
        p[3] = static_cast<uint8_t>(v);
    }

    uint64_t client_key(const sockaddr_in &a) {
        return (static_cast<uint64_t>(a.sin_addr.s_addr) << 16) | a.sin_port;
    }
}

audio_stream_server::audio_stream_server(audio_stream_config cfg)
        : config(cfg) {
    frame_size = config.sample_rate / 1000 * config.frame_ms;

    int err = 0;
    encoder = opus_encoder_create(static_cast<opus_int32>(config.sample_rate),
                                  config.channels, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || encoder == nullptr) {
        throw std::runtime_error(std::string("opus_encoder_create failed: ") + opus_strerror(err));
    }
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(config.bitrate));
    // In-band FEC (LBRR): embed a low-bitrate copy of the previous frame in each
    // packet so a client that sees a sequence gap can reconstruct one lost frame
    // from the next packet (opus_decode with decode_fec=1). The encoder only spends
    // bits on FEC when it expects loss, so the loss-percent estimate must be > 0.
    if (config.fec_loss_perc > 0) {
        opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(config.fec_loss_perc));
    }

    if (!open_socket()) {
        throw std::runtime_error("audio: failed to bind UDP socket on port "
                                 + std::to_string(config.port));
    }

    LOG(INFO) << "audio streaming " << config.sample_rate << "Hz/" << config.channels
              << "ch opus@" << config.bitrate << "bps"
              << (config.fec_loss_perc > 0 ? " +FEC" : "")
              << " to subscribers on port " << config.port;

    is_running = true;
    worker_thread = std::thread(&audio_stream_server::work, this);
}

audio_stream_server::~audio_stream_server() {
    stop();
    close_capture();
    if (encoder != nullptr) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
}

bool audio_stream_server::open_socket() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        LOG(ERROR) << "audio: socket creation failed: " << std::strerror(errno);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config.port);
    if (bind(sockfd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0) {
        LOG(ERROR) << "audio: bind on port " << config.port << " failed: " << std::strerror(errno);
        return false;
    }

    // Non-blocking so draining subscription datagrams never stalls the capture cadence.
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

bool audio_stream_server::open_capture() {
    // Blocking open: this runs on the worker thread, so a slow open never stalls
    // the main loop, and snd_pcm_open already fails fast on a missing device.
    int err = snd_pcm_open(&pcm, config.device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        pcm = nullptr;
        return false;
    }

    // Configure with explicit hw_params rather than snd_pcm_set_params(). The
    // latter's small, latency-derived period (~25 ms at a 100 ms target) makes
    // some USB audio CODECs (e.g. the IC-7300's) return -EIO on *every* read, so
    // capture never starts. A larger period/buffer — what arecord negotiates —
    // captures cleanly; we read in opus-frame chunks regardless of the period.
    snd_pcm_hw_params_t *hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, config.channels);
    unsigned int rate = config.sample_rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
    snd_pcm_uframes_t period = config.sample_rate / 8;   // ~125 ms
    snd_pcm_uframes_t buffer = period * 4;               // ~500 ms
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);

    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) {
        LOG(WARNING) << "audio: snd_pcm_hw_params failed: " << snd_strerror(err);
        snd_pcm_close(pcm);
        pcm = nullptr;
        return false;
    }
    if (rate != config.sample_rate) {
        LOG(WARNING) << "audio: device capture rate " << rate
                     << " differs from configured " << config.sample_rate;
    }
    return true;
}

void audio_stream_server::close_capture() {
    if (pcm != nullptr) {
        snd_pcm_close(pcm);
        pcm = nullptr;
    }
}

void audio_stream_server::poll_clients() {
    // Drain every pending subscription datagram. Any datagram (re)registers its
    // sender; the payload is irrelevant, so the smallest possible keepalive works.
    uint8_t scratch[MAX_PACKET];
    while (true) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        auto n = recvfrom(sockfd, scratch, sizeof(scratch), 0,
                          reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n < 0) {
            break;  // EAGAIN/EWOULDBLOCK: nothing left to read
        }
        auto now = clock.ellapsed_ms();
        auto key = client_key(from);
        auto it = clients.find(key);
        if (it == clients.end()) {
            LOG(INFO) << "audio: new subscriber " << inet_ntoa(from.sin_addr) << ":"
                      << ntohs(from.sin_port) << " (" << clients.size() + 1 << " total)";
            clients[key] = client{from, now};
        } else {
            it->second.last_seen_ms = now;
        }
    }

    // Expire clients that have gone silent past the timeout.
    auto now = clock.ellapsed_ms();
    for (auto it = clients.begin(); it != clients.end();) {
        if (now - it->second.last_seen_ms >= static_cast<uint32_t>(config.client_timeout_ms)) {
            LOG(INFO) << "audio: dropping silent subscriber " << inet_ntoa(it->second.addr.sin_addr)
                      << ":" << ntohs(it->second.addr.sin_port);
            it = clients.erase(it);
        } else {
            ++it;
        }
    }
}

void audio_stream_server::broadcast(const uint8_t *payload, size_t len) {
    if (clients.empty() || len == 0 || payload == nullptr) {
        return;
    }
    uint8_t packet[MAX_PACKET];
    if (HEADER_BYTES + len > sizeof(packet)) {
        return;
    }
    put_be32(packet, sequence++);
    memcpy(packet + HEADER_BYTES, payload, len);
    for (auto &entry : clients) {
        sendto(sockfd, packet, HEADER_BYTES + len, 0,
               reinterpret_cast<const sockaddr *>(&entry.second.addr), sizeof(entry.second.addr));
    }
}

void audio_stream_server::work() {
    std::vector<int16_t> pcm_buf(static_cast<size_t>(frame_size) * config.channels);
    uint8_t opus_buf[MAX_PACKET];

    while (is_running) {
        if (pcm == nullptr) {
            if (!open_capture()) {
                if (capture_was_open) {
                    LOG(WARNING) << "audio: capture device " << config.device << " unavailable";
                    capture_was_open = false;
                }
                // No audio to send, but keep tracking subscribers so they can join /
                // refresh (and expire) while we wait for the device to reappear.
                poll_clients();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            LOG(INFO) << "audio: capturing from " << config.device;
            capture_was_open = true;
        }

        auto frames = snd_pcm_readi(pcm, pcm_buf.data(), frame_size);
        if (frames == -EPIPE) {
            // Overrun: we did not drain the device fast enough. Recover and continue.
            snd_pcm_prepare(pcm);
            continue;
        }
        if (frames < 0) {
            LOG(WARNING) << "audio: read failed (" << snd_strerror(frames) << "), reopening device";
            close_capture();
            continue;
        }

        // One subscription/expiry sweep per frame (~every frame_ms) is plenty responsive.
        poll_clients();

        auto encoded = opus_encode(encoder, pcm_buf.data(), static_cast<int>(frames),
                                   opus_buf, sizeof(opus_buf));
        if (encoded < 0) {
            LOG(WARNING) << "audio: opus_encode failed: " << opus_strerror(encoded);
            continue;
        }
        broadcast(opus_buf, static_cast<size_t>(encoded));
    }
}

void audio_stream_server::update() {
    // Streaming is driven by the worker thread; nothing to do on the main loop.
}

void audio_stream_server::stop() {
    if (!is_running.exchange(false)) {
        return;
    }
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    LOG(INFO) << "audio stream server shut down";
}
