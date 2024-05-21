#include "libs/easylogging++.h"
#include "cwsd.h"
#include "libs/node.hpp"

void usage();
cwsd_config read_config(std::string path);
void configure_logging(el::Level level, std::string filename, std::string max_file_size);

el::Level to_logging_level(std::string level_as_str);
INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv) {
    try {
        auto config = read_config("~/.config/cwsdrc");
        configure_logging(to_logging_level(config.logging.level), config.logging.filename, config.logging.max_size);
        cwsd driver(config);
        driver.run();
    } catch (std::exception &e) {
        LOG(ERROR) << e.what();
        return 1;
    }
    return 0;
}

el::Level to_logging_level(std::string level_as_str) {
    std::transform(level_as_str.begin(), level_as_str.end(), level_as_str.begin(), ::tolower);
    if (level_as_str == "info") {
        return el::Level::Info;
    } else if (level_as_str == "debug") {
        return el::Level::Debug;
    } else if (level_as_str == "error") {
        return el::Level::Error;
    } else if (level_as_str == "warning") {
        return el::Level::Warning;
    }
    return el::Level::Info;
}

void usage() {
    std::cerr << "Usage: cwsd /path/to/device" << std::endl;
    exit(1);
}

cwsd_config read_config(std::string path) {
    if (!path.empty() && path[0] == '~') {
        path = std::string(getenv("HOME")) + path.substr(1);
    }

    std::ifstream ifs(path);
    if (!ifs) {
        std::stringstream ss;
        ss << "could not find config file " << path;
        throw std::runtime_error(ss.str());
    }

    fkyaml::node root = fkyaml::node::deserialize(ifs);

    cwsd_config cfg;

    cfg.rig.model = root["rig"]["model"].get_value<int>();
    cfg.rig.port = root["rig"]["port"].get_value<std::string>();

    cfg.cwdaemon.enabled = root["cwdaemon"]["enabled"].get_value<bool>();
    cfg.cwdaemon.port = root["cwdaemon"]["port"].get_value<int>();

    cfg.rigctld.enabled = root["rigctld"]["enabled"].get_value<bool>();
    cfg.rigctld.port = root["rigctld"]["port"].get_value<int>();

    if (root.contains("logging")) {
        auto log_node = root["logging"];
        if (log_node.contains("level")) {
            cfg.logging.level = log_node["level"].get_value<std::string>();
        }
        if (log_node.contains("filename")) {
            cfg.logging.filename = log_node["filename"].get_value<std::string>();
        }

        if (log_node.contains("max_size")) {
            cfg.logging.max_size = log_node["max_size"].get_value<std::string>();
        }
    }

    return cfg;
}

void pre_rollout_callback(const char *full_path, std::size_t s) {
    const int how_many_logs = 10;

    std::string newer = std::string(full_path) + ".?";
    std::string older = std::string(full_path) + ".?";
    auto digit_pos = newer.size() - 1;
    for (int i = 9; i > 0; i--) {
        older[digit_pos] = char('0' + (i - 1));
        newer[digit_pos] = char('0' + i);
        rename(older.c_str(), newer.c_str());
    }

    older[digit_pos] = '1';
    rename(full_path, older.c_str());
}

void configure_logging(el::Level level, std::string filename, std::string max_file_size) {
    el::Loggers::addFlag(el::LoggingFlag::HierarchicalLogging);
    el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
    el::Loggers::setLoggingLevel(level);

    el::Configurations log_conf;
    log_conf.setToDefault();
    log_conf.setGlobally(el::ConfigurationType::Format, "%datetime %level %msg");
    log_conf.setGlobally(el::ConfigurationType::Filename, filename);
    log_conf.setGlobally(el::ConfigurationType::MaxLogFileSize, max_file_size);
    el::Loggers::reconfigureAllLoggers(log_conf);

    el::Helpers::installPreRollOutCallback(pre_rollout_callback);

    LOG(INFO) << "--------------------------------------------------------------------------------------------";
}