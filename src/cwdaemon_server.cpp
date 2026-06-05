#include <string>
#include "cwdaemon_server.h"
#include "keyer/keyer.h"
#include "timer.h"
#include <sys/ioctl.h>
#include <thread>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include "libs/easylogging++.h"
#include "cw_daemon.h"
#include "udp_server.h"

namespace {
    // Promote the calling thread to real-time scheduling and lock the process memory so the
    // keyer's element timing is not stretched by ordinary scheduler preemption or page faults.
    // Requires CAP_SYS_NICE / a suitable RLIMIT_MEMLOCK (granted by the systemd unit or root);
    // on failure we log and keep running at normal priority rather than aborting.
    void try_set_realtime_priority() {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            LOG(WARNING) << "cwdaemon: mlockall failed (" << std::strerror(errno)
                         << "); keying may jitter under memory pressure";
        }

        sched_param sp{};
        sp.sched_priority = sched_get_priority_min(SCHED_FIFO) + 10;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
            LOG(WARNING) << "cwdaemon: could not enable real-time scheduling for the keyer thread ("
                         << std::strerror(errno)
                         << "); run as root or grant CAP_SYS_NICE for jitter-free keying";
        } else {
            LOG(INFO) << "cwdaemon: keyer thread running at real-time priority " << sp.sched_priority;
        }
    }
}

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
    // Keying is driven by the dedicated keyer_worker() thread (see keyer_worker), not by the
    // shared main-loop cadence, so there is nothing to do here.
}

void cwdaemon_server::keyer_worker() {
    try_set_realtime_priority();

    // Open the serial control lines straight away rather than waiting for the first throttled
    // check below.
    iface->update();

    // Tick the keyer state machine at a fine, fixed cadence. The element key-up edge can only
    // land on a tick, so a tighter interval (here 1 ms) plus the real-time priority above keeps
    // element lengths close to their nominal duration.
    int iface_check_divider = 0;
    while (is_running) {
        keyer::update();

        // Connect/disconnect detection (isatty + possible reopen) is comparatively heavy and
        // does not need the keyer's cadence; run it roughly every 100 ms.
        if (++iface_check_divider >= 100) {
            iface_check_divider = 0;
            iface->update();
        }

        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
}

cwdaemon_server::cwdaemon_server(std::string device, uint16_t listen_port, int initial_wpm) {
    iface = new key_interface(device, clock);
    keyer::init(iface);


// Workaround for keyer::set_speed()
// TODO: find out why set_speed() does not perform as intended here
    keyer::winkeyer_data(0x02);
    keyer::winkeyer_data(initial_wpm);

    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    server = new udp_server(listen_port, timeout);

    is_running = true;
    keyer_thread = std::thread(&cwdaemon_server::keyer_worker, this);
    worker_thread = std::thread(&cwdaemon_server::client_worker, this, iface, server, &clock);
}

void cwdaemon_server::stop() {
    is_running = false;
    if (keyer_thread.joinable()) {
        keyer_thread.join();
    }
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    LOG(INFO) << "cwdaemon server shut down";
}
