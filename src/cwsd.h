#ifndef CWSD_CWSD_H
#define CWSD_CWSD_H

#include <string>
#include <atomic>
#include "rigctld_server.h"
#include "cwdaemon_server.h"
#include <csignal>

struct cwsd_config {
    struct {
        bool enabled = false;
        int port;
    } cwdaemon;
    struct {
        bool enabled = false;
        int port;
    } rigctld;
    struct {
        std::string port;
        int model;
    } rig;
};


class cwsd {
public:
    void initialize_signal_handler();
    explicit cwsd(cwsd_config cfg);
    ~cwsd();

    static void signal_handler(int s);

    void run();
private:
    cwsd_config config{};
    static std::atomic<bool> is_running;
    rigctld_server *rigctld = nullptr;
    cwdaemon_server *cwdaemon = nullptr;
    struct sigaction sigint_hndlr;
};


#endif //CWSD_CWSD_H
