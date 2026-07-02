// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "cwsd.h"
#include "libs/easylogging++.h"

cwsd::cwsd(cwsd_config cfg)
        : config(cfg) {
    initialize_signal_handler();
    if (cfg.rigctld.enabled) {
        rigctld = new rigctld_server(cfg.rig.port, cfg.rig.model, cfg.rigctld.port);
    }
    if (cfg.cwdaemon.enabled) {
        cwdaemon = new cwdaemon_server(cfg.rig.port, cfg.cwdaemon.port, cfg.cwdaemon.initial_wpm);
    }
    if (cfg.audio.enabled) {
        audio = new audio_stream_server(cfg.audio);
    }
    if (cfg.remote_key.enabled) {
        remote_key = new remote_key_server(cfg.remote_key);
    }
}

cwsd::~cwsd() {
    delete remote_key;
    delete audio;
    delete cwdaemon;
    delete rigctld;
}

std::atomic<bool> cwsd::is_running{false};

void cwsd::run() {
    is_running = true;
    while (is_running) {
        if (config.rigctld.enabled) {
            rigctld->update();
        }
        if (config.cwdaemon.enabled) {
            cwdaemon->update();
        }
        if (config.audio.enabled) {
            audio->update();
        }
        if (config.remote_key.enabled) {
            remote_key->update();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (config.rigctld.enabled) {
        rigctld->stop();
    }
    if (config.cwdaemon.enabled) {
        cwdaemon->stop();
    }
    if (config.audio.enabled) {
        audio->stop();
    }
    if (config.remote_key.enabled) {
        remote_key->stop();
    }

}

void cwsd::initialize_signal_handler() {
    sigint_hndlr.sa_handler = cwsd::signal_handler;
    sigemptyset(&sigint_hndlr.sa_mask);
    sigint_hndlr.sa_flags = 0;

    sigaction(SIGINT, &sigint_hndlr, nullptr);
    sigaction(SIGKILL, &sigint_hndlr, nullptr);
    sigaction(SIGTERM, &sigint_hndlr, nullptr);
}

void cwsd::signal_handler(int s) {
    LOG(INFO) << "caught signal " << s << ". closing";
    is_running = false;
}
