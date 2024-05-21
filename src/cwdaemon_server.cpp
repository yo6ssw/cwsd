#include <string>
#include "cwdaemon_server.h"
#include "keyer/keyer.h"
#include "timer.h"
#include <sys/ioctl.h>
#include <thread>
#include <fcntl.h>
#include "easylogging++.h"
#include "cw_daemon.h"
#include "udp_server.h"

key_interface::key_interface(const std::string d, timer &t)
        : device(d), clock{t} {
}

bool key_interface::open_port() {
    fd = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }

    key = TIOCM_DTR;
    ptt = TIOCM_RTS;

    int m = 0;
    ioctl(fd, TIOCMGET, &m);

//  on_key_up(); // not ok to invoke virtual methods from constructor
    ioctl(fd, TIOCMBIC, &key);
    return true;
}

uint32_t key_interface::current_ms() {
    return clock.ellapsed_ms();
}

bool key_interface::is_paddle_pressed(keyer::paddle_side side) {
    return false;
}

void key_interface::on_key_down() {
    ioctl(fd, TIOCMBIS, &key);
}

void key_interface::on_key_up() {
    ioctl(fd, TIOCMBIC, &key);
}

void key_interface::update() {
    auto fd_valid = isatty(fd);
    if (fd_valid && !fd_was_valid) {
        LOG(INFO) << "cwdaemon connected to port " << device;
    } else if (!fd_valid && fd_was_valid) {
        LOG(INFO) << "cwdaemon disconnected from port " << device;
    }
    if (!fd_valid) {
        open_port();
    }
    fd_was_valid = fd_valid;
}

void cwdaemon_server::client_worker(key_interface *iface, udp_server *server, timer *clock) {
    bool is_tuning = false;
    uint32_t tune_end_time = 0;

    is_running = true;
    while (is_running) {
        if (server->receive()) {
            auto speed = static_cast<unsigned char>(keyer::get_speed());
            auto message = server->last_message();

            bool is_abort_send_cmd = message.size() > 1 && message[0] == 27 && message[1] == '4';
            bool is_exit_cmd = message.size() > 1 && message[0] == 27 && message[1] == '5';

            if (is_exit_cmd) {
                LOG(INFO) << "cwdaemon received exit command. shutting down";
                break;
            }

            // if abort send cmd is sent, but we are currently tuning
            // stop the tuning
            if (is_tuning && is_abort_send_cmd) {
                keyer::winkeyer_data(0x0B);
                keyer::winkeyer_data(0x0);
                is_tuning = false;
                continue;
            }

            auto wk_data = cw_daemon::to_winkeyer(message, speed);
            if (cw_daemon::is_tuning_command(wk_data)) {
                is_tuning = true;
                // cwdaemon ^c command needs a parameter of tune seconds
                // We are translating that into winkeyer immediate key on/off commands which
                // do not take a time parameter, so we need to remove it from the stream after
                // we use it to know when to stop the tuning.
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


void cwdaemon_server::set_event_bus(event_bus *bus) {
}

void cwdaemon_server::update() {
    keyer::update();
    iface->update();
}

cwdaemon_server::cwdaemon_server(std::string device, uint16_t listen_port) {
    iface = new key_interface(device, clock);
    keyer::init(iface);
    keyer::set_speed(30);

    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    server = new udp_server(listen_port, timeout);
    worker_thread = std::thread(&cwdaemon_server::client_worker, this, iface, server, &clock);
}

void cwdaemon_server::stop() {
    is_running = false;
    worker_thread.join();
    LOG(INFO) << "cwdaemon server shut down";
}
