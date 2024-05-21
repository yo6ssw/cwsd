#include "libs/easylogging++.h"
#include "cwsd.h"
#include "libs/node.hpp"

void usage();
cwsd_config get_config(std::string path);
void configure_logging();

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
    }

    configure_logging();
    try {
        cwsd driver(get_config("./config.yml"));
        driver.run();
    } catch (std::exception &e) {
        LOG(ERROR) << e.what();
        return 1;
    }
    return 0;
}

void usage() {
    std::cerr << "Usage: cwsd /path/to/device" << std::endl;
    exit(1);
}

cwsd_config get_config(std::string path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("could not find config file");
    }

    fkyaml::node root = fkyaml::node::deserialize(ifs);

    cwsd_config cfg;

    cfg.rig.model = root["rig"]["model"].get_value<int>();
    cfg.rig.port = root["rig"]["port"].get_value<std::string>();

    cfg.cwdaemon.enabled = root["cwdaemon"]["enabled"].get_value<bool>();
    cfg.cwdaemon.port = root["cwdaemon"]["port"].get_value<int>();

    cfg.rigctld.enabled = root["rigctld"]["enabled"].get_value<bool>();
    cfg.rigctld.port = root["rigctld"]["port"].get_value<int>();

    return cfg;
}

void configure_logging() {
    el::Loggers::addFlag(el::LoggingFlag::HierarchicalLogging);
    el::Loggers::setLoggingLevel(el::Level::Debug);

    el::Configurations log_conf;
    log_conf.setToDefault();
    log_conf.setGlobally(el::ConfigurationType::Format, "%datetime %level %msg");
    el::Loggers::reconfigureAllLoggers(log_conf);
}