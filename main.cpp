// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "cwsd.h"
#include "cwsdver-GitVersion.h"
#include "libs/easylogging++.h"
#include "libs/node.hpp"

void usage();
cwsd_config read_config(std::string path);
void configure_logging(el::Level level, std::string filename,
                       std::string max_file_size);
static std::string cwsd_version_string();

el::Level to_logging_level(std::string level_as_str);
void daemonize();

INITIALIZE_EASYLOGGINGPP

int main(int argc, char** argv) {
  std::string config_path = "~/.config/cwsdrc";
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      daemonize();
    } else if (strcmp(argv[i], "--version") == 0) {
      std::cout << "cwsd v" << cwsd_version_string() << std::endl;
      exit(EXIT_SUCCESS);
    } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
      if (i + 1 >= argc) {
        std::cerr << argv[i] << " requires a path argument" << std::endl;
        exit(EXIT_FAILURE);
      }
      config_path = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
    } else {
      std::cerr << "unknown argument: " << argv[i] << std::endl;
      usage();
    }
  }

  try {
    auto config = read_config(config_path);
    configure_logging(to_logging_level(config.logging.level),
                      config.logging.filename,
                      std::to_string(config.logging.max_size));
    cwsd driver(config);
    driver.run();
  } catch (std::exception& e) {
    LOG(ERROR) << e.what();
    return 1;
  }
  return 0;
}

void daemonize() {
  // fork off the parent process
  auto pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);

  // success: ;et the parent terminate
  if (pid > 0) exit(EXIT_SUCCESS);

  // on success: the child process becomes session leader
  if (setsid() < 0) exit(EXIT_FAILURE);

  // fork off for the second time
  pid = fork();
  if (pid < 0) exit(EXIT_FAILURE);

  // success: Let the parent terminate
  if (pid > 0) exit(EXIT_SUCCESS);

  // set new file permissions
  umask(0);
  chdir("/");

  // close all open file descriptors
  for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
    close(x);
  }
}

el::Level to_logging_level(std::string level_as_str) {
  std::transform(level_as_str.begin(), level_as_str.end(), level_as_str.begin(),
                 ::tolower);
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
  std::cerr << "Usage: cwsd [options]\n"
               "  -c, --config <path>   config file (default: ~/.config/cwsdrc)\n"
               "  -d                    daemonize (double-fork)\n"
               "      --version         print version and exit\n"
               "  -h, --help            show this help\n";
  exit(EXIT_FAILURE);
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
  if (root["rig"].contains("serial_speed")) {
    cfg.rig.serial_speed = root["rig"]["serial_speed"].get_value<int>();
  }

  cfg.cwdaemon.enabled = root["cwdaemon"]["enabled"].get_value<bool>();
  cfg.cwdaemon.port = root["cwdaemon"]["port"].get_value<int>();
  cfg.cwdaemon.initial_wpm = root["cwdaemon"]["initial_wpm"].get_value<int>();

  cfg.rigctld.enabled = root["rigctld"]["enabled"].get_value<bool>();
  cfg.rigctld.port = root["rigctld"]["port"].get_value<int>();

  if (root.contains("audio")) {
    auto audio_node = root["audio"];
    cfg.audio.enabled = audio_node["enabled"].get_value<bool>();
    if (cfg.audio.enabled) {
      cfg.audio.port = audio_node["port"].get_value<uint16_t>();
      if (audio_node.contains("device")) {
        cfg.audio.device = audio_node["device"].get_value<std::string>();
      }
      if (audio_node.contains("sample_rate")) {
        cfg.audio.sample_rate = audio_node["sample_rate"].get_value<uint32_t>();
      }
      if (audio_node.contains("channels")) {
        cfg.audio.channels = audio_node["channels"].get_value<int>();
      }
      if (audio_node.contains("bitrate")) {
        cfg.audio.bitrate = audio_node["bitrate"].get_value<int>();
      }
      if (audio_node.contains("frame_ms")) {
        cfg.audio.frame_ms = audio_node["frame_ms"].get_value<int>();
      }
      if (audio_node.contains("client_timeout_ms")) {
        cfg.audio.client_timeout_ms =
            audio_node["client_timeout_ms"].get_value<int>();
      }
      if (audio_node.contains("fec_loss_perc")) {
        cfg.audio.fec_loss_perc = audio_node["fec_loss_perc"].get_value<int>();
      }
    }
  }

  if (root.contains("remote_key")) {
    auto rk_node = root["remote_key"];
    cfg.remote_key.enabled = rk_node["enabled"].get_value<bool>();
    if (cfg.remote_key.enabled) {
      cfg.remote_key.port = rk_node["port"].get_value<int>();
      // Falls back to the shared rig serial device when not given its own.
      cfg.remote_key.device = rk_node.contains("device")
                                  ? rk_node["device"].get_value<std::string>()
                                  : cfg.rig.port;
      if (rk_node.contains("playout_ms")) {
        cfg.remote_key.playout_ms = rk_node["playout_ms"].get_value<uint32_t>();
      }
      if (rk_node.contains("silence_ms")) {
        cfg.remote_key.silence_ms = rk_node["silence_ms"].get_value<uint32_t>();
      }
      if (rk_node.contains("max_key_down_ms")) {
        cfg.remote_key.max_key_down_ms =
            rk_node["max_key_down_ms"].get_value<uint32_t>();
      }
      if (rk_node.contains("ptt_lead_ms")) {
        cfg.remote_key.ptt_lead_ms =
            rk_node["ptt_lead_ms"].get_value<uint32_t>();
      }
      if (rk_node.contains("ptt_tail_ms")) {
        cfg.remote_key.ptt_tail_ms =
            rk_node["ptt_tail_ms"].get_value<uint32_t>();
      }
    }
  }

  if (root.contains("logging")) {
    auto log_node = root["logging"];
    if (log_node.contains("level")) {
      cfg.logging.level = log_node["level"].get_value<std::string>();
    }
    if (log_node.contains("filename")) {
      cfg.logging.filename = log_node["filename"].get_value<std::string>();
    }

    if (log_node.contains("max_size")) {
      cfg.logging.max_size = log_node["max_size"].get_value<uint32_t>();
    }
  }

  return cfg;
}

void pre_rollout_callback(const char* full_path, std::size_t s) {
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

std::string cwsd_version_string() {
  std::stringstream ss;
  ss << cwsdver::version_string()
     << (cwsdver::version_distance() > 0
             ? std::to_string(cwsdver::version_distance())
             : "")
     << " (" << cwsdver::version_shorthash()
     << (cwsdver::version_isdirty() ? "-dirty" : "") << ")";
  return ss.str();
}

// Severity rank for our log levels (low = chatty, high = severe). NOTE: this is
// the intuitive order; easylogging++'s own hierarchy ranks Info ABOVE Warning/
// Error, which is why HierarchicalLogging used to silently hide warnings and
// errors when the configured level was "info".
static int severity_of(el::Level level) {
  switch (level) {
    case el::Level::Trace:
      return 0;
    case el::Level::Debug:
      return 1;
    case el::Level::Verbose:
      return 1;
    case el::Level::Info:
      return 2;
    case el::Level::Warning:
      return 3;
    case el::Level::Error:
      return 4;
    case el::Level::Fatal:
      return 5;
    default:
      return 2;
  }
}

void configure_logging(el::Level level, std::string filename,
                       std::string max_file_size) {
  el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);

  el::Configurations log_conf;
  log_conf.setToDefault();
  log_conf.setGlobally(el::ConfigurationType::Format, "%datetime %level %msg");
  log_conf.setGlobally(el::ConfigurationType::Filename, filename);
  log_conf.setGlobally(el::ConfigurationType::MaxLogFileSize, max_file_size);

  // Enable the configured level and everything MORE severe, set explicitly per
  // level so Warning/Error/Fatal are never hidden (unlike HierarchicalLogging,
  // which would suppress them at level "info").
  const int threshold = severity_of(level);
  auto enable = [&](el::Level l) {
    log_conf.set(l, el::ConfigurationType::Enabled,
                 severity_of(l) >= threshold ? "true" : "false");
  };
  enable(el::Level::Trace);
  enable(el::Level::Debug);
  enable(el::Level::Verbose);
  enable(el::Level::Info);
  enable(el::Level::Warning);
  enable(el::Level::Error);
  enable(el::Level::Fatal);
  el::Loggers::reconfigureAllLoggers(log_conf);

  el::Helpers::installPreRollOutCallback(pre_rollout_callback);

  LOG(INFO) << "---------------------------------------------------------------"
               "-----------------------------";
  LOG(INFO) << "starting cwsd v" << cwsd_version_string();
}