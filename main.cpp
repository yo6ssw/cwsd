#include <chrono>
#include <iostream>
#include "keyer/keyer.h"
#include <bits/stdc++.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "udp_server.h"
#include "cw_daemon.h"
#include "timer.h"


struct key_interface : keyer::hw_interface {
    int fd;
    int key;
    int ptt;
    const timer &clock;

    key_interface(const char *device, timer &t)
            : clock{t} {
        std::cout << "- opening device " << device << std::endl;
        fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "- failed to open " << device << std::endl;
            exit(1);
        }

        key = TIOCM_DTR;
        ptt = TIOCM_RTS;

        int m = 0;
        ioctl(fd, TIOCMGET, &m);

        on_key_up();
    }

    uint32_t current_ms() override {
        return clock.ellapsed_ms();
    }

    bool is_paddle_pressed(keyer::paddle_side side) override {
        return false;
    }

    void on_key_down() override {
        ioctl(fd, TIOCMBIS, &key);
    }

    void on_key_up() override {
        ioctl(fd, TIOCMBIC, &key);
    }
};

void usage() {
    std::cerr << "Usage: ./cwkeyer /path/to/device" << std::endl;
    exit(1);
}

std::atomic<bool> is_running = true;

void client_worker(udp_server *server, timer *clock) {
    // TODO: fix clean termination
    bool is_tuning = false;
    uint32_t tune_end_time = 0;
    while (is_running) {
        if (server->receive()) {
            auto speed = static_cast<unsigned char>(keyer::get_speed());
            auto message = server->last_message();
            if (message.size() > 1 && message[0] == 27 && message[1] == '5') {
                std::cout << "- received exit command. shutting down" << std::endl;
                is_running = false;
                break;
            }
            auto wk_data = cw_daemon::to_winkeyer(message, speed);
            if (cw_daemon::is_tuning_command(wk_data)) {
                is_tuning = true;
                // cwdaemon ^c command needs a parameter of tune seconds
                // we are translating that into winkeyer immediate key on/off commands
                // which do not take a time parameter
                tune_end_time = clock->ellapsed_ms() + 1000 * wk_data.back();
                wk_data.pop_back();
            }
            for (auto &c: wk_data) {
                keyer::winkeyer_data(c);
            }
        }
        if (is_tuning && clock->ellapsed_ms() > tune_end_time) {
            keyer::winkeyer_data(0x0B);
            keyer::winkeyer_data(0x0);
            is_tuning = false;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
    }

    timer clock;
    key_interface iface{argv[1], clock};
    keyer::init(&iface);
    keyer::set_speed(30);

    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    udp_server server{6789, timeout};
    std::thread t(client_worker, &server, &clock);

    // TODO: figure out a way for graceful shutdown. signal handlers + ctrl interface?
    while (is_running) {
        keyer::update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t.join();
}
