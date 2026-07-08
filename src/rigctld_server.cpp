// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#include "rigctld_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <hamlib/rig.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include "libs/easylogging++.h"
#include "string_util.h"

void rigctld_server::update() {}

static int debug_callback(enum rig_debug_level_e level, rig_ptr_t user_data,
                          const char* message, va_list args);

rigctld_server::rigctld_server(std::string dev, int model, uint16_t port,
                               int serial_speed)
    : device(dev),
      rig_model(model),
      listen_port(port),
      serial_speed(serial_speed) {
  start_listener();
  rig_set_debug(RIG_DEBUG_ERR);
  rig_set_debug_callback(debug_callback, this);
  worker = std::thread(&rigctld_server::work, this);
}

void rigctld_server::start_listener() {
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error("failed to create rigctld socket");
  }

  LOG(DEBUG) << "setting rigctld listener to nonblocking";
  fcntl(server_fd, F_SETFL, O_NONBLOCK);

  //    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout,
  //    sizeof(read_timeout)) < 0) {
  //        throw std::runtime_error("failed to set read timeout");
  //    }

  LOG(DEBUG) << "setting rigctld listener to reuse addr";
  int reuseOption = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseOption,
             sizeof(reuseOption));

  sockaddr_in serv_addr{};
  memset(&serv_addr, 0, sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;  // IPv4
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(listen_port);

  if (bind(server_fd, reinterpret_cast<const struct sockaddr*>(&serv_addr),
           sizeof(serv_addr)) < 0) {
    throw std::runtime_error("failed to bind rigctld listener");
  }

  if (listen(server_fd, 5) != 0) {
    throw std::runtime_error("rigctld listen() failed");
  }
  LOG(INFO) << "rigctld listening on port " << listen_port;
}

void rigctld_server::work() {
  update_poll_descriptors();

  is_running = true;
  while (is_running) {
    if (rig_disconnected) {
      if (rig != nullptr) {
        for (auto& c : clients) {
          this->close_client(c.second);
        }
        clients.clear();
        update_poll_descriptors();

        // Note: close and clean-up *must* be issued from the very same thread
        // that issued init() and open()!
        LOG(INFO) << "rigctld device disconnected (" << device << ")";
        rig_close(rig);
        rig_cleanup(rig);
        rig = nullptr;
      }

      if (open_rig()) {
        LOG(INFO) << "rigctld device connected (" << device << ")";
        rig_disconnected.store(false);
      }
    }

    auto num_events = poll(pfds, pfd_nr, 1000);  // timeout in ms
    if (num_events > 0) {
      int fd_to_add = -1;
      std::vector<int> fds_to_remove;

      for (auto i = 0; i < pfd_nr; i++) {
        if (poll_declares_error(pfds[i].revents)) {
          LOG(ERROR) << "[c:" << pfds[i].fd << "] detected error." << std::endl;
          fds_to_remove.push_back(pfds[i].fd);
        } else {
          if (pfds[i].revents & POLLIN) {
            if (i == 0) {
              fd_to_add = this->accept_client();
            } else {
              if (!this->read_from_client(clients[pfds[i].fd])) {
                fds_to_remove.push_back(pfds[i].fd);
              }
            }
          }

          if (pfds[i].revents & POLLOUT) {
            if (!this->write_from_client(clients[pfds[i].fd])) {
              fds_to_remove.push_back(pfds[i].fd);
            }
          }
        }
      }

      for (auto& r : fds_to_remove) {
        auto it = clients.find(r);
        if (it != clients.end()) {
          this->close_client(it->second);
          clients.erase(it);
        }
      }

      if (fd_to_add > 0 || !fds_to_remove.empty()) {
        update_poll_descriptors();
      }

      this->update_poll_flags();
    }
  }
}

static int debug_callback(enum rig_debug_level_e level, rig_ptr_t user_data,
                          const char* message, va_list args) {
  auto disconnected = string_contains(message, "read failed");
  if (disconnected) {
    auto self = reinterpret_cast<rigctld_server*>(user_data);
    self->on_rig_disconnect();
  }
  return 0;
}

bool rigctld_server::open_rig() {
  rig = rig_init(rig_model);
  if (rig == nullptr) {
    LOG(DEBUG) << "rig_init returned null";
    throw std::runtime_error("unknown rig num");
  }
  // sizeof the field rather than FILPATHLEN: Hamlib 4.6+ renamed that constant
  // to HAMLIB_FILPATHLEN, and sizeof works against both old and new headers.
  strncpy(rig->state.rigport.pathname, device.c_str(),
          sizeof(rig->state.rigport.pathname) - 1);

  int result;

  // Pin the serial rate when configured. Some hamlib backends default to a rate
  // the local termios layer can't set (the QMX backend defaults to 256000, which
  // serial_setup rejects -> rig_open returns -2 and the rig never connects). On
  // USB CDC-ACM the value is ignored by the hardware, so any standard rate works.
  if (serial_speed != 0) {
    result = rig_set_conf(rig, rig_token_lookup(rig, "serial_speed"),
                          std::to_string(serial_speed).c_str());
    if (result != RIG_OK) {
      throw std::runtime_error("rigctld failed to set serial_speed");
    }
  }

  result = rig_set_conf(rig, rig_token_lookup(rig, "rts_state"), "OFF");
  if (result != RIG_OK) {
    throw std::runtime_error("rigctld failed to set rts_state");
  }
  result = rig_set_conf(rig, rig_token_lookup(rig, "dtr_state"), "OFF");
  if (result != RIG_OK) {
    throw std::runtime_error("rigctld failed to set dtr_state");
  }

  result = rig_open(rig);
  if (result != RIG_OK) {
    //        LOG(DEBUG) << "rig_open returns " << result << std::endl;
    rig_cleanup(rig);
    rig = nullptr;
    return false;
  }

  return true;
}

void rigctld_server::on_rig_disconnect() {
  // A CI-V read fails for two very different reasons, and Hamlib's "read
  // failed" message can't tell them apart: the USB cable was pulled (the
  // device node vanishes), or the rig was merely switched to standby — still
  // on the bus, just no longer answering CI-V. Only the former is a real
  // disconnect. Tearing the connection down for standby too would close the
  // serial port and stop interpreting client commands (see read_from_client),
  // so we could never deliver the \set_powerstat 1 that wakes the rig back up
  // — remote power-on would be impossible. Treat the rig as gone only when its
  // device node has actually disappeared; otherwise leave the handle open so
  // the power-on command still reaches the (sleeping but present) radio.
  if (access(device.c_str(), F_OK) == 0)
    return;  // device still present -> rig is asleep, not unplugged

  bool expected = false;
  if (rig_disconnected.compare_exchange_strong(expected, true)) {
    // disconnected just now
  }
}

void rigctld_server::stop() {
  is_running = false;
  worker.join();
  LOG(INFO) << "rigctld server shut down";
}

bool rigctld_server::interpret_command(std::string& command, int client_fd) {
  auto cmd = trim(command);
  LOG(DEBUG) << "[c:" << client_fd << "] >> " << cmd;

  if (cmd == "\\get_powerstat") {
    powerstat_t status;
    rig_get_powerstat(rig, &status);
    send_response_to_client(to_client(status), client_fd);
  } else if (starts_with(cmd, "\\set_powerstat ")) {
    // arg: 0=off, 1=on, 2=standby, 4=operate (hamlib powerstat_t)
    auto status = static_cast<powerstat_t>(std::stoi(&cmd.c_str()[15]));
    rig_set_powerstat(rig, status);
    send_response_to_client("RPRT 0", client_fd);
  } else if (cmd == "\\chk_vfo") {
    // TODO: don't hardcode this
    send_response_to_client("0", client_fd);
  } else if (cmd == "\\get_lock_mode") {
    int lock;
    rig_get_lock_mode(rig, &lock);
    send_response_to_client(std::to_string(lock), client_fd);
    send_response_to_client("", client_fd);  // yes, it needs an extra LF
  } else if (cmd == "\\dump_state") {
    send_response_to_client(dump_state(), client_fd);
  } else if (cmd == "f") {
    freq_t freq;
    rig_get_freq(rig, RIG_VFO_CURR, &freq);
    send_response_to_client(std::to_string(freq), client_fd);
  } else if (cmd == "s") {
    // Default to simplex: WSJT-X "Fake It" split refuses unless the rig reports
    // simplex, and rig_get_split_vfo() fails on some backends (e.g. IC-7300),
    // leaving the seed values — so don't trust it without checking the return.
    split_t split = RIG_SPLIT_OFF;
    vfo_t txvfo = RIG_VFO_A;
    if (rig_get_split_vfo(rig, RIG_VFO_CURR, &split, &txvfo) != RIG_OK ||
        txvfo == RIG_VFO_NONE) {
      split = RIG_SPLIT_OFF;
      txvfo = RIG_VFO_A;
    }
    // WSJT-X expects a concrete TX VFO; the IC-7300 backend hands back a VFO we
    // don't render (-> "UNKNOWN"), so normalise anything but A/B/C to VFOA.
    if (txvfo != RIG_VFO_A && txvfo != RIG_VFO_B && txvfo != RIG_VFO_C)
      txvfo = RIG_VFO_A;
    std::stringstream ss;
    ss << split_to_string(split) << "\n" << vfo_to_string(txvfo);
    send_response_to_client(ss.str(), client_fd);
  } else if (starts_with(cmd, "S ")) {
    // set split: "S <0|1> <txvfo>". WSJT-X "Fake It" sets simplex; honor it on
    // the rig and always ack — an unanswered set would stall the client.
    auto pieces = split_string(cmd, " ");
    split_t split =
        (pieces.size() > 1 && pieces[1] == "1") ? RIG_SPLIT_ON : RIG_SPLIT_OFF;
    vfo_t txvfo =
        (pieces.size() > 2) ? vfo_from_string(pieces[2].c_str()) : RIG_VFO_A;
    rig_set_split_vfo(rig, RIG_VFO_CURR, split, txvfo);
    send_response_to_client("RPRT 0", client_fd);
  } else if (cmd == "t") {
    ptt_t ptt;
    rig_get_ptt(rig, RIG_VFO_CURR, &ptt);
    send_response_to_client(std::to_string(ptt), client_fd);
  } else if (starts_with(cmd, "T ")) {
    ptt_t ptt = static_cast<ptt_t>(std::stoi(&cmd.c_str()[2]));
    rig_set_ptt(rig, RIG_VFO_CURR, ptt);
    send_response_to_client("RPRT 0", client_fd);
  } else if (starts_with(cmd, "V ")) {
    rig_set_vfo(rig, vfo_from_string(&cmd.c_str()[2]));
    send_response_to_client("RPRT 0", client_fd);
  } else if (cmd == "m") {
    rmode_t mode = 0;
    pbwidth_t width = 0;
    rig_get_mode(rig, RIG_VFO_CURR, &mode, &width);
    std::stringstream ss;
    ss << mode_to_string(mode) << "\n" << width;
    send_response_to_client(ss.str(), client_fd);
  } else if (starts_with(cmd, "M ")) {
    // M PKTUSB -1
    auto pieces = split_string(cmd, " ");
    auto pbwidth = std::stoi(pieces[2]);
    rig_set_mode(rig, RIG_VFO_CURR, mode_from_string(pieces[1].c_str()),
                 pbwidth);
    send_response_to_client("RPRT 0", client_fd);
  } else if (starts_with(cmd, "F ")) {
    freq_t freq = std::stod(&cmd.c_str()[2]);
    rig_set_freq(rig, RIG_VFO_CURR, freq);
    send_response_to_client("RPRT 0", client_fd);
  } else if (starts_with(cmd, "l ")) {
    // get level: "l <LEVEL>" e.g. SWR / RFPOWER / RFPOWER_METER / STRENGTH /
    // ALC. Previously hardcoded to a constant, so SWR/power meters read
    // garbage; read the real value via hamlib. rig_parse_level maps the name to
    // the level token; float levels (SWR, RFPOWER, ALC, *_METER) report a
    // float, int levels (STRENGTH) an int — exactly as real rigctld does.
    setting_t level = rig_parse_level(&cmd.c_str()[2]);
    value_t val{};
    int rc = rig_get_level(rig, RIG_VFO_CURR, level, &val);
    std::string resp;
    if (rc != RIG_OK)
      resp = "RPRT " + std::to_string(rc);
    else if (RIG_LEVEL_IS_FLOAT(level))
      resp = std::to_string(val.f);
    else
      resp = std::to_string(val.i);
    send_response_to_client(resp, client_fd);
  } else if (starts_with(cmd, "L ")) {
    // set level: "L <LEVEL> <value>", e.g. "L AGC 0" to disable AGC, "L AGC 5"
    // (RIG_AGC_MEDIUM) to re-enable it. Mirrors the "l " getter above:
    // rig_parse_level maps the name to the level token; float levels take a
    // float value, int levels (AGC, ATT, ...) an int — as real rigctld does.
    auto pieces = split_string(cmd, " ");
    setting_t level =
        rig_parse_level(pieces.size() > 1 ? pieces[1].c_str() : "");
    value_t val{};
    if (RIG_LEVEL_IS_FLOAT(level))
      val.f = pieces.size() > 2 ? std::stof(pieces[2]) : 0.0f;
    else
      val.i = pieces.size() > 2 ? std::stoi(pieces[2]) : 0;
    // The IC-7300 can't switch AGC fully OFF over CAT (only FAST/MID/SLOW), so
    // map a disable request to FAST — the least level-flattening setting, which
    // is what a CW skimmer wants. The model is known here (not to a netrigctl
    // client like xlog2), so the rig-specific quirk lives in cwsd.
    if (level == RIG_LEVEL_AGC && val.i == RIG_AGC_OFF &&
        rig_model == RIG_MODEL_IC7300)
      val.i = RIG_AGC_FAST;
    int rc = rig_set_level(rig, RIG_VFO_CURR, level, val);
    send_response_to_client("RPRT " + std::to_string(rc), client_fd);
  } else if (cmd == "q" || cmd == "Q") {
    LOG(INFO) << "[c:" << client_fd << "] quit.";
    // Real rigctld replies "RPRT 0" to the quit command. Hamlib's
    // netrigctl_close sends "q\n" and reads that reply; without it the
    // client's read times out (EAGAIN) and its Hamlib crashes on close.
    // Flush synchronously here since the connection is about to be closed.
    send_response_to_client("RPRT 0", client_fd);
    write_from_client(clients[client_fd]);
    return false;
  } else if (cmd == "j") {
    shortfreq_t rit = 0;
    rig_get_rit(rig, RIG_VFO_CURR, &rit);
    send_response_to_client(std::to_string(rit), client_fd);
  } else if (cmd == "v") {
    send_response_to_client("Main", client_fd);
  } else {
    LOG(ERROR) << "[c:" << client_fd << "] unhandled command [" << cmd << "]";
  }
  return true;
}

std::string rigctld_server::to_client(powerstat_t status) {
  std::string result;
  result += char('0' + status);
  return result;
}

void rigctld_server::send_response_to_client(const std::string& response,
                                             int fd) {
  LOG(DEBUG) << "[c:" << fd << "] << " << response;
  clients[fd].write_buffer.append(response);
  clients[fd].write_buffer.push_back('\n');
}

std::string rigctld_server::vfo_to_string(vfo_t vfo) {
  switch (vfo) {
    case RIG_VFO_A:
      return "VFOA";
    case RIG_VFO_B:
      return "VFOB";
    case RIG_VFO_C:
      return "VFOC";
    default:
      return "UNKNOWN";
  }
}

vfo_t rigctld_server::vfo_from_string(const char* vfo_name) {
  if (strcmp(vfo_name, "VFOA") == 0) {
    return RIG_VFO_A;
  } else if (strcmp(vfo_name, "VFOB") == 0) {
    return RIG_VFO_B;
  } else if (strcmp(vfo_name, "VFOC") == 0) {
    return RIG_VFO_C;
  } else {
    return RIG_VFO_CURR;
  }
}

const char* rigctld_server::mode_to_string(rmode_t mode) {
  switch (mode) {
    case RIG_MODE_USB:
      return "USB";
    case RIG_MODE_LSB:
      return "LSB";
    case RIG_MODE_CW:
      return "CW";
    case RIG_MODE_RTTY:
      return "RTTY";
    case RIG_MODE_RTTYR:
      return "RTTYR";
    case RIG_MODE_AM:
      return "AM";
    case RIG_MODE_FM:
      return "FM";
    case RIG_MODE_PKTLSB:
      return "PKTLSB";
    case RIG_MODE_PKTUSB:
      return "PKTUSB";
    case RIG_MODE_PKTFM:
      return "PKTFM";
    default:
      return "UNKNOWN";
  }
}

rmode_t rigctld_server::mode_from_string(const char* name) {
  if (strcmp(name, "USB") == 0)
    return RIG_MODE_USB;
  else if (strcmp(name, "LSB") == 0)
    return RIG_MODE_LSB;
  else if (strcmp(name, "CW") == 0)
    return RIG_MODE_CW;
  else if (strcmp(name, "RTTY") == 0)
    return RIG_MODE_RTTY;
  else if (strcmp(name, "RTTYR") == 0)
    return RIG_MODE_RTTYR;
  else if (strcmp(name, "AM") == 0)
    return RIG_MODE_AM;
  else if (strcmp(name, "FM") == 0)
    return RIG_MODE_FM;
  else if (strcmp(name, "PKTLSB") == 0)
    return RIG_MODE_PKTLSB;
  else if (strcmp(name, "PKTUSB") == 0)
    return RIG_MODE_PKTUSB;
  else if (strcmp(name, "PKTFM") == 0)
    return RIG_MODE_PKTFM;
  else
    return RIG_MODE_ALL;
}

const char* rigctld_server::split_to_string(split_t split) {
  if (split == RIG_SPLIT_ON)
    return "1";
  else
    return "0";
}

void rigctld_server::update_poll_descriptors() {
  if (nullptr != pfds) {
    delete[] pfds;
  }

  pfds = new pollfd[clients.size() + 1];
  pfds[0].fd = server_fd;
  pfds[0].events = POLLIN;

  int index = 1;
  for (auto& c : clients) {
    pfds[index].fd = c.second.fd;
    pfds[index].events = POLLIN;
    if (!c.second.write_buffer.empty()) {
      pfds[index].events |= POLLOUT;
    }
    index++;
  }

  pfd_nr = clients.size() + 1;
}

int rigctld_server::accept_client() {
  sockaddr_in sa{};
  socklen_t sa_size = sizeof(sa);

  int client_fd = accept(server_fd, (sockaddr*)&sa, &sa_size);
  if (client_fd < 0) {
    LOG(ERROR) << "rigctld accept() on rigctl listener returned " << client_fd;
  } else {
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN);

    fcntl(client_fd, F_SETFL, O_NONBLOCK);

    LOG(INFO) << "[c:" << client_fd << "] rigctld client connected from " << str
              << ":" << sa.sin_port;
    clients.insert({client_fd, rigctld_client{.fd = client_fd,
                                              .read_buffer = "",
                                              .write_buffer = "",
                                              .is_running = true}});
  }

  return client_fd;
}

bool rigctld_server::read_from_client(rigctld_client& client) {
  char msg[128];

  auto bytes_read = recv(client.fd, (char*)&msg, sizeof(msg) - 1, 0);
  if (bytes_read < 0) {
    LOG(WARNING) << "[c:" << client.fd << "] recv() error (errno=" << errno
                 << ")";
    return false;
  }

  if (bytes_read == 0) {
    LOG(INFO) << "[c:" << client.fd << "] EOF detected";
    return false;
  }

  msg[bytes_read] = '\0';
  client.read_buffer.append(msg);

  // do not interpret client requests while rig is disconnected
  if (!rig_disconnected) {
    size_t pos;
    while ((pos = client.read_buffer.find('\n')) != std::string::npos) {
      auto cmd = client.read_buffer.substr(0, pos);
      // A malformed argument must never take down the whole service. The
      // parsers below lean on std::stoi/std::stod, which throw on garbage
      // (e.g. "F abc", or the VFO-prefixed form "M VFOA CW 1200" that a
      // vfo-mode rigctld client sends — our std::stoi would hit "CW").
      // An uncaught throw unwinds out of work() and kills the worker
      // thread, dropping CAT for every connected client. Contain it to the
      // one bad command and answer with an error, as real rigctld does.
      try {
        if (!interpret_command(cmd, client.fd)) {
          return false;
        }
      } catch (const std::exception& e) {
        LOG(ERROR) << "[c:" << client.fd << "] command [" << cmd
                   << "] failed: " << e.what();
        send_response_to_client("RPRT -1", client.fd);
      }
      client.read_buffer.erase(0, cmd.size() + 1);
    }
  }

  return true;
}

bool rigctld_server::write_from_client(rigctld_client& client) {
  if (client.write_buffer.empty()) return true;

  auto written_bytes =
      write(client.fd, client.write_buffer.c_str(), client.write_buffer.size());
  if (written_bytes < 0) {
    LOG(ERROR) << "[c:" << client.fd << "] error writing";
    return false;
  }

  client.write_buffer.erase(0, written_bytes);
  return true;
}

void rigctld_server::close_client(rigctld_client& client) {
  LOG(INFO) << "rigctld closing client " << client.fd;
  shutdown(client.fd, SHUT_RDWR);
  close(client.fd);
}

bool rigctld_server::poll_declares_error(short events) {
  return events & (POLLERR | POLLHUP | POLLNVAL);
}

rigctld_server::~rigctld_server() { delete[] pfds; }

void rigctld_server::update_poll_flags() {
  auto index = 1;
  for (auto& c : clients) {
    if (c.second.write_buffer.empty()) {
      pfds[index].events &= ~POLLOUT;
    } else {
      pfds[index].events |= POLLOUT;
    }
    index++;
  }
}

// int rig_sprintf_agc_levels(RIG *rig, char *str, int lenstr) {
//     auto priv_caps = (const struct icom_priv_caps *) rig->caps->priv;
//
//     int len = 0;
//     int i;
//     char tmpbuf[256];
//
//     str[len] = 0;
//
//     if (priv_caps && RIG_BACKEND_NUM(rig->caps->rig_model) == RIG_ICOM &&
//     priv_caps->agc_levels_present) {
//         for (i = 0; i <= HAMLIB_MAX_AGC_LEVELS &&
//         priv_caps->agc_levels[i].level != RIG_AGC_LAST; i++) {
//             if (strlen(str) > 0) { strcat(str, " "); }
//
//             snprintf(tmpbuf, sizeof(tmpbuf), "%d=%s",
//                      priv_caps->agc_levels[i].icom_level,
//                      rig_stragclevel(priv_caps->agc_levels[i].level));
//
//             if (strlen(str) + strlen(tmpbuf) < lenstr - 1) {
//                 strncat(str, tmpbuf, lenstr - 1);
//             } else {
//                 rig_debug(RIG_DEBUG_ERR, "%s: buffer overrun!!  len=%d >
//                 maxlen=%d\n",
//                           __func__, (int) (strlen(str) + strlen(tmpbuf)),
//                           lenstr - 1);
//             }
//         }
//     } else {
//         for (i = 0; i < HAMLIB_MAX_AGC_LEVELS && i <
//         rig->caps->agc_level_count; i++) {
//             if (strlen(str) > 0) { strcat(str, " "); }
//
//             snprintf(tmpbuf, sizeof(tmpbuf), "%d=%s",
//                      rig->caps->agc_levels[i],
//                      rig_stragclevel(rig->caps->agc_levels[i]));
//             if (strlen(str) + strlen(tmpbuf) < lenstr - 1) {
//                 strncat(str, tmpbuf, lenstr - 1);
//             } else {
//                 rig_debug(RIG_DEBUG_ERR, "%s: buffer overrun!!  len=%d >
//                 maxlen=%d\n",
//                           __func__, (int) (strlen(str) + strlen(tmpbuf)),
//                           lenstr - 1);
//             }
//         }
//     }
//
//     return strlen(str);
// }

std::string rigctld_server::dump_state() {
  // What follows is a 1 to 1 translation from HamLib commit b4ec8a42
  std::stringstream result;
  // Protocol version 0: Hamlib's netrigctl client reads the whole positional
  // block below (ranges/steps/filters/preamp/attenuator/has_*) and then
  // returns immediately, BEFORE its key=value parsing loop. That loop is the
  // buggy path (e.g. agc_levels[i++] with an un-reset index, plus calloc into
  // shared static caps) that corrupts the client's heap and crashes it on
  // close. Version 0 keeps all the useful positional caps while skipping the
  // entire key=value section. (See Hamlib 4.6.5 rigs/dummy/netrigctl.c:626.)
  const int RIGCTLD_PROT_VER = 0;
  result << std::to_string(RIGCTLD_PROT_VER) << "\n";
  result << std::to_string(rig->caps->rig_model) << "\n";

  result << "0\n";  // deprecated itu_region

  struct rig_state* rs = &rig->state;
  for (auto i = 0;
       i < HAMLIB_FRQRANGESIZ && !RIG_IS_FRNG_END(rs->rx_range_list[i]); i++) {
    result << format("%lf %lf 0x%llx %d %d 0x%x 0x%x\n",
                     rs->rx_range_list[i].startf, rs->rx_range_list[i].endf,
                     rs->rx_range_list[i].modes, rs->rx_range_list[i].low_power,
                     rs->rx_range_list[i].high_power, rs->rx_range_list[i].vfo,
                     rs->rx_range_list[i].ant);
  }

  result << format("0 0 0 0 0 0 0\n");

  for (auto i = 0;
       i < HAMLIB_FRQRANGESIZ && !RIG_IS_FRNG_END(rs->tx_range_list[i]); i++) {
    result << format("%lf %lf 0x%llx %d %d 0x%x 0x%x\n",
                     rs->tx_range_list[i].startf, rs->tx_range_list[i].endf,
                     rs->tx_range_list[i].modes, rs->tx_range_list[i].low_power,
                     rs->tx_range_list[i].high_power, rs->tx_range_list[i].vfo,
                     rs->tx_range_list[i].ant);
  }

  result << format("0 0 0 0 0 0 0\n");

  for (auto i = 0; i < HAMLIB_TSLSTSIZ && !RIG_IS_TS_END(rs->tuning_steps[i]);
       i++) {
    result << format("0x%llx %ld\n", rs->tuning_steps[i].modes,
                     rs->tuning_steps[i].ts);
  }

  result << format("0 0\n");

  for (auto i = 0; i < HAMLIB_FLTLSTSIZ && !RIG_IS_FLT_END(rs->filters[i]);
       i++) {
    result << format("0x%llx %ld\n", rs->filters[i].modes,
                     rs->filters[i].width);
  }

  result << format("0 0\n");

  result << format("%ld\n", rs->max_rit);
  result << format("%ld\n", rs->max_xit);
  result << format("%ld\n", rs->max_ifshift);
  result << format("%d\n", rs->announces);

  for (auto i = 0; i < HAMLIB_MAXDBLSTSIZ && rs->preamp[i]; i++) {
    result << format("%d ", rs->preamp[i]);
  }

  result << format("\n");

  for (auto i = 0; i < HAMLIB_MAXDBLSTSIZ && rs->attenuator[i]; i++) {
    result << format("%d ", rs->attenuator[i]);
  }

  result << format("\n");

  result << format("0x%llx\n", rs->has_get_func);
  result << format("0x%llx\n", rs->has_set_func);
  result << format("0x%llx\n", rs->has_get_level);
  result << format("0x%llx\n", rs->has_set_level);
  result << format("0x%llx\n", rs->has_get_parm);
  result << format("0x%llx\n", rs->has_set_parm);

  // Protocol version 0 ends here: the client returns right after the block
  // above and must not receive the key=value section (vfo_ops/ptt_type/...
  // agc_levels/ctcss_list/dcs_list/level_gran/parm_gran/done), whose client
  // parser corrupts its heap. If this is ever raised to protocol 1, that
  // section must be re-added — and only after the Hamlib netrigctl client
  // bugs are fixed upstream.

  return result.str();
}
