#include "easylogging++.h"
#include <chrono>
#include <iostream>
#include <sys/syslog.h>
#include "cwsd.h"

void usage() {
    std::cerr << "Usage: cwsd /path/to/device" << std::endl;
    exit(1);
}

cwsd_config get_config();

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv) {
    ELPP_INITIALIZE_SYSLOG("cwsd", LOG_PID | LOG_CONS | LOG_PERROR, LOG_USER);
    if (argc < 2) {
        usage();
    }

    try {
        cwsd driver(get_config());
        driver.run();
    } catch (std::exception &e) {
        SYSLOG(ERROR) << e.what();
        return 1;
    }
    return 0;
}

cwsd_config get_config() {
    cwsd_config cfg = {
            .cwdaemon = {
                    .enabled = true,
                    .port = 6789
            },
            .rigctld = {
                    .enabled = true,
                    .port = 4532
            },
            .rig = {
                    .port = "/dev/icom7300",
                    .model = 3073
            }
    };
    return cfg;
}
