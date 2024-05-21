#include "cwsd.h"
#include "libs/easylogging++.h"

cwsd::cwsd(cwsd_config cfg)
        : config(cfg) {
    initialize_signal_handler();
    if (cfg.cwdaemon.enabled) {
        cwdaemon = new cwdaemon_server(cfg.rig.port, cfg.cwdaemon.port);
    }
    if (cfg.rigctld.enabled) {
        rigctld = new rigctld_server(cfg.rig.port, cfg.rig.model, cfg.rigctld.port);
    }
}

cwsd::~cwsd() {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (config.rigctld.enabled) {
        rigctld->stop();
    }
    if (config.cwdaemon.enabled) {
        cwdaemon->stop();
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
