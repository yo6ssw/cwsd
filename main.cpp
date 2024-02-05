#include <chrono>
#include <iostream>
#include "keyer/keyer.h"
#include <bits/stdc++.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "udp_server.h"
#include "cw_daemon.h"


typedef std::chrono::high_resolution_clock Clock;

struct key_interface : keyer::hw_interface {
    int fd;
    int key;
    int ptt;
    std::chrono::time_point<std::chrono::system_clock> start;

    key_interface(const char *device) {
        std::cout << "- opening device " << device << std::endl;
        fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "- failed to open " << device << std::endl;
            exit(1);
        }

        start = Clock::now();

        key = TIOCM_DTR;
        ptt = TIOCM_RTS;

        int m = 0;
        ioctl(fd, TIOCMGET, &m);

        on_key_up();
    }

    uint32_t current_ms() override {
        const auto now = Clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
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

void client_worker(udp_server *server) {
    // TODO: fix clean termination
    for (;;) {
        if (server->receive()) {
            auto speed = static_cast<unsigned char>(keyer::get_speed());
            auto wk_data = cw_daemon::to_winkeyer(server->last_message(), speed);
            for (auto &c: wk_data) {
                keyer::winkeyer_data(c);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
    }

    key_interface iface{argv[1]};
    keyer::init(&iface);
    keyer::set_speed(30);

    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    udp_server server{6789, timeout};
    std::thread t(client_worker, &server);

    // TODO: figure out a way for graceful shutdown. signal handlers + ctrl interface?
    while (true) {
        keyer::update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t.join();
}
