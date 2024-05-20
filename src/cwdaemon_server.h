//
// Created by benny on 14/05/24.
//

#ifndef CWSD_CWDAEMON_SERVER_H
#define CWSD_CWDAEMON_SERVER_H


#include <thread>
#include <atomic>
#include "events.h"
#include "keyer/keyer.h"
#include "timer.h"
#include "udp_server.h"

struct key_interface;

class cwdaemon_server {
public:
    explicit cwdaemon_server(std::string device, uint16_t listen_port);
    void set_event_bus(event_bus *bus);
    void update();
    void client_worker(key_interface *iface, udp_server *server, timer *clock);

    key_interface* iface;
    udp_server* server;
    timer clock;
    std::thread worker_thread;
    std::atomic<bool> is_running;
    void stop();
};

struct key_interface : keyer::hw_interface {
    int fd = -1;
    int key;
    int ptt;
    const timer &clock;
    std::string device;
    bool fd_was_valid = false;

    bool connected() const { return fd_was_valid; }

    key_interface(std::string device, timer &t);
    bool open_port();
    uint32_t current_ms() override;
    bool is_paddle_pressed(keyer::paddle_side side) override;
    void on_key_down() override;
    void on_key_up() override;
    void update();
};


#endif //CWSD_CWDAEMON_SERVER_H
