#include <thread>
#include <sys/socket.h>
#include "rigctld_server.h"
#include <fcntl.h>
#include <netinet/in.h>
#include "easylogging++.h"
#include "string_util.h"
#include <poll.h>
#include <arpa/inet.h>

void rigctld_server::update() {
}

static int debug_callback(enum rig_debug_level_e level, rig_ptr_t user_data, const char *message, va_list args);

rigctld_server::rigctld_server(std::string dev, int model, uint16_t port)
        : device(dev), rig_model(model), listen_port(port) {

    start_listener();
    rig_set_debug(RIG_DEBUG_ERR);
    rig_set_debug_callback(debug_callback, this);
    worker = std::thread(&rigctld_server::work, this);
}

void rigctld_server::start_listener() {
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        throw std::runtime_error("failed to create rigctld socket");
    }

    SYSLOG(INFO) << "setting rigctld listener to nonblocking" << std::endl;
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

//    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout)) < 0) {
//        throw std::runtime_error("failed to set read timeout");
//    }

    SYSLOG(INFO) << "setting rigctld listener to reuse addr" << std::endl;
    int reuseOption = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseOption, sizeof(reuseOption));

    sockaddr_in serv_addr{};
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET; // IPv4
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(listen_port);

    if (bind(server_fd, reinterpret_cast<const struct sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0) {
        throw std::runtime_error("failed to bind rigctld listener");
    }

    if (listen(server_fd, 5) != 0) {
        throw std::runtime_error("rigctld listen() failed");
    }
    SYSLOG(INFO) << "rigctld listening on port " << listen_port << std::endl;
}


void rigctld_server::work() {
    update_poll_descriptors();

    is_running = true;
    while (is_running) {
        if (rig_disconnected) {
            if (rig != nullptr) {
                for (auto &c: clients) {
                    this->close_client(c.second);
                }
                clients.clear();
                update_poll_descriptors();

                // Note: close and clean-up *must* be issued from the very same thread that issued init() and open()!
                SYSLOG(INFO) << "rigctld device disconnected (" << device << ")" << std::endl;
                rig_close(rig);
                rig_cleanup(rig);
                rig = nullptr;
            }

            if (open_rig()) {
                SYSLOG(INFO) << "rigctld device connected (" << device << ")" << std::endl;
                rig_disconnected.store(false);
            }
        }
        auto num_events = poll(pfds, pfd_nr, 10); // timeout in ms
        if (num_events > 0) {
            int fd_to_add = -1;
            std::vector<int> fds_to_remove;

            for (auto i = 0; i < pfd_nr; i++) {
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

                if (poll_declares_error(pfds[i].revents)) {
                    SYSLOG(ERROR) << "[c:" << pfds[i].fd << "] detected error." << std::endl;
                    fds_to_remove.push_back(pfds[i].fd);
                }
            }

            for (auto &r: fds_to_remove) {
                this->close_client(clients[r]);
                clients.erase(r);
            }

            if (fd_to_add > 0 || fds_to_remove.size() > 0) {
                update_poll_descriptors();
            }
        }
    }
}


static int debug_callback(enum rig_debug_level_e level, rig_ptr_t user_data, const char *message, va_list args) {
    auto disconnected = string_contains(message, "read failed");
    if (disconnected) {
        auto self = reinterpret_cast<rigctld_server *>(user_data);
        self->on_rig_disconnect();
    }
    return 0;
}

bool rigctld_server::open_rig() {
    rig = rig_init(rig_model);
    if (rig == nullptr) {
        SYSLOG(DEBUG) << "rig_init returned null" << std::endl;
        throw std::runtime_error("unknown rig num");
    }
    strncpy(rig->state.rigport.pathname, device.c_str(), FILPATHLEN - 1);

    int result = rig_open(rig);
    if (result != RIG_OK) {
//        SYSLOG(DEBUG) << "rig_open returns " << result << std::endl;
        rig_cleanup(rig);
        rig = nullptr;
        return false;
    }

    result = rig_set_conf(rig, rig_token_lookup(rig, "rts_state"), "OFF");
    if (result != RIG_OK) {
        throw std::runtime_error("rigctld failed to set rts_state");
    }
    result = rig_set_conf(rig, rig_token_lookup(rig, "dtr_state"), "OFF");
    if (result != RIG_OK) {
        throw std::runtime_error("rigctld failed to set dtr_state");
    }
    return true;
}

void rigctld_server::on_rig_disconnect() {
    bool expected = false;
    if (rig_disconnected.compare_exchange_strong(expected, true)) {
        // disconnected just now
    }
}

void rigctld_server::stop() {
    is_running = false;
    worker.join();
    SYSLOG(INFO) << "rigctld server shut down" << std::endl;
}


void rigctld_server::interpret_command(std::string &command, int client_fd) {
    auto cmd = trim(command);
    SYSLOG(DEBUG) << "[c:" << client_fd << "] >> " << cmd;

    if (cmd == "\\get_powerstat") {
        powerstat_t status;
        rig_get_powerstat(rig, &status);
        send_response_to_client(to_client(status), client_fd);
    } else if (cmd == "\\chk_vfo") {
        // TODO: don't hardcode this
        send_response_to_client("0", client_fd);
    } else if (cmd == "\\get_lock_mode") {
        int lock;
        rig_get_lock_mode(rig, &lock);
        send_response_to_client(std::to_string(lock), client_fd);
    } else if (cmd == "\\dump_state") {
        // TODO: don't hardcode this
        send_response_to_client(
                "1\n3073\n0\n30000.000000 74800000.000000 0x401dbf -1 -1 0x10000003 0x0\n0 0 0 0 0 0 0\n1810000.000000 2000000.000000 0x1be 2000 100000 0x10000003 0x1\n3500000.000000 3800000.000000 0x1be 2000 100000 0x10000003 0x1\n7000000.000000 7200000.000000 0x1be 2000 100000 0x10000003 0x1\n10100000.000000 10150000.000000 0x1be 2000 100000 0x10000003 0x1\n14000000.000000 14350000.000000 0x1be 2000 100000 0x10000003 0x1\n18068000.000000 18168000.000000 0x1be 2000 100000 0x10000003 0x1\n21000000.000000 21450000.000000 0x1be 2000 100000 0x10000003 0x1\n24890000.000000 24990000.000000 0x1be 2000 100000 0x10000003 0x1\n28000000.000000 29700000.000000 0x1be 2000 100000 0x10000003 0x1\n5351500.000000 5366500.000000 0x1be 2000 100000 0x10000003 0x1\n50000000.000000 54000000.000000 0x1be 2000 100000 0x10000003 0x1\n70000000.000000 70500000.000000 0x1be 2000 50000 0x10000003 0x1\n1810000.000000 2000000.000000 0x400001 1000 25000 0x10000003 0x1\n3500000.000000 3800000.000000 0x400001 1000 25000 0x10000003 0x1\n7000000.000000 7200000.000000 0x400001 1000 25000 0x10000003 0x1\n10100000.000000 10150000.000000 0x400001 1000 25000 0x10000003 0x1\n14000000.000000 14350000.000000 0x400001 1000 25000 0x10000003 0x1\n18068000.000000 18168000.000000 0x400001 1000 25000 0x10000003 0x1\n21000000.000000 21450000.000000 0x400001 1000 25000 0x10000003 0x1\n24890000.000000 24990000.000000 0x400001 1000 25000 0x10000003 0x1\n28000000.000000 29700000.000000 0x400001 1000 25000 0x10000003 0x1\n5351500.000000 5366500.000000 0x400001 1000 25000 0x10000003 0x1\n50000000.000000 54000000.000000 0x400001 1000 25000 0x10000003 0x1\n70000000.000000 70500000.000000 0x400001 1000 12500 0x10000003 0x1\n0 0 0 0 0 0 0\n0x401dbf 1\n0x401dbf 1000\n0x401dbf 5000\n0x401dbf 9000\n0x401dbf 10000\n0x401dbf 12500\n0x401dbf 20000\n0x401dbf 25000\n0 0\n0xc0c 2400\n0xc0c 1800\n0xc0c 3000\n0x192 500\n0x192 250\n0x82 1200\n0x110 2400\n0x400001 6000\n0x400001 3000\n0x400001 9000\n0x1020 10000\n0x1020 7000\n0x1020 15000\n0 0\n9999\n9999\n0\n0\n1 2 \n20 \n0xfc00c90133fe\n0xfc00c90133fe\n0xc7fff74677f3f\n0xc7f7000677f3f\n0x35\n0x35\nvfo_ops=0x81f\nptt_type=0x1\ntargetable_vfo=0x3\nhas_set_vfo=1\nhas_get_vfo=0\nhas_set_freq=1\nhas_get_freq=1\nhas_set_conf=1\nhas_get_conf=1\nhas_power2mW=1\nhas_mW2power=1\ntimeout=1000\nrig_model=3073\nrigctld_version=Hamlib 4.5.5 Apr 05 11:43:08Z 2023 SHA=6eecd3\nagc_levels=0=OFF 1=FAST 2=MEDIUM 3=SLOW\nctcss_list= 60.0 67.0 69.3 71.9 74.4 77.0 79.7 82.5 85.4 88.5 91.5 94.8 97.4 100.0 103.5 107.2 110.9 114.8 118.8 120.0 123.0 127.3 131.8 136.5 141.3 146.2 151.4 156.7 159.8 162.2 165.5 167.9 171.3 173.8 177.3 179.9 183.5 186.2 189.9 192.8 196.6 199.5 203.5 206.5 210.7 218.1 225.7 229.1 233.6 241.8 250.3 254.1\ndone",
                client_fd);
    } else if (cmd == "f") {
        freq_t freq;
        rig_get_freq(rig, RIG_VFO_CURR, &freq);
        send_response_to_client(std::to_string(freq), client_fd);
    } else if (cmd == "s") {
        split_t split = RIG_SPLIT_ON;
        vfo_t txvfo = 0;
        rig_get_split_vfo(rig, RIG_VFO_CURR, &split, &txvfo);
        std::stringstream ss;
        ss << split_to_string(split) << "\n" << vfo_to_string(txvfo);
        send_response_to_client(ss.str(), client_fd);
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
        rig_set_mode(rig, RIG_VFO_CURR, mode_from_string(pieces[1].c_str()), pbwidth);
        send_response_to_client("RPRT 0", client_fd);
    } else if (starts_with(cmd, "F ")) {
        freq_t freq = std::stod(&cmd.c_str()[2]);
        rig_set_freq(rig, RIG_VFO_CURR, freq);
        send_response_to_client("RPRT 0", client_fd);
    } else {
        SYSLOG(ERROR) << "[c:" << client_fd << "] unhandled command [" << cmd << "]" << std::endl;
    }
}

std::string rigctld_server::to_client(powerstat_t status) {
    std::string result;
    result += char('0' + status);
    return result;
}

void rigctld_server::send_response_to_client(const std::string &response, int fd) {
    SYSLOG(DEBUG) << "[c:" << fd << "] << " << response;
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

vfo_t rigctld_server::vfo_from_string(const char *vfo_name) {
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

const char *rigctld_server::mode_to_string(rmode_t mode) {
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

rmode_t rigctld_server::mode_from_string(const char *name) {
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

const char *rigctld_server::split_to_string(split_t split) {
    if (split == RIG_SPLIT_ON)
        return "1";
    else
        return "0";
}

void rigctld_server::update_poll_descriptors() {
    delete[] pfds;

    pfds = new pollfd[clients.size() + 1];
    pfds[0].fd = server_fd;
    pfds[0].events = POLLIN;

    int index = 1;
    for (auto &c: clients) {
        pfds[index].fd = c.second.fd;
        pfds[index].events = POLLIN | POLLOUT;
        index++;
    }

    pfd_nr = clients.size() + 1;
}

int rigctld_server::accept_client() {
    sockaddr_in sa{};
    socklen_t sa_size = sizeof(sa);

    int client_fd = accept(server_fd, (sockaddr *) &sa, &sa_size);
    if (client_fd < 0) {
        SYSLOG(ERROR) << "rigctld accept() on rigctl listener returned " << client_fd << std::endl;
    } else {
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN);

        SYSLOG(INFO) << "[c:" << client_fd << "] rigctld client connected from " << str << ":" << sa.sin_port
                     << std::endl;
        clients.insert({client_fd, rigctld_client{
                .fd = client_fd,
                .read_buffer = "",
                .write_buffer = "",
                .is_running = true
        }});
    }

    return client_fd;
}

bool rigctld_server::read_from_client(rigctld_client &client) {
    char msg[128];
    memset(&msg, 0, sizeof(msg));

    auto bytes_read = recv(client.fd, (char *) &msg, sizeof(msg) - 1, 0);
    if (bytes_read < 0) {
        SYSLOG(WARNING) << "[c:" << client.fd << "] recv() returned " << bytes_read << std::endl;
        return false;
    }

    if (bytes_read > 0) {
        msg[bytes_read] = '\0';
        client.read_buffer.append(msg);
    }

    // do not interpret client requests while rig is disconnected
    if (!rig_disconnected) {
        size_t pos;
        while ((pos = client.read_buffer.find('\n')) != std::string::npos) {
            auto cmd = client.read_buffer.substr(0, pos);
            interpret_command(cmd, client.fd);
            client.read_buffer.erase(0, cmd.size() + 1);
        }
    }

    return true;

    // recv() returning 0 when socket is blocking means the client disconnected
    SYSLOG(WARNING) << "[c:" << client.fd << "] disconnected" << std::endl;
    return false;
}

bool rigctld_server::write_from_client(rigctld_client &client) {
    if (client.write_buffer.empty()) return true;

    auto written_bytes = write(client.fd, client.write_buffer.c_str(), client.write_buffer.size());
    if (written_bytes < 0) {
        SYSLOG(ERROR) << "[c:" << client.fd << "] error writing" << std::endl;
        return false;
    }

    client.write_buffer.erase(0, written_bytes);
    return true;
}

void rigctld_server::close_client(rigctld_client &client) {
    SYSLOG(INFO) << "rigctld closing client " << client.fd << std::endl;
    shutdown(client.fd, SHUT_RDWR);
    close(client.fd);
}

bool rigctld_server::poll_declares_error(short events) {
    return events & (POLLERR | POLLHUP | POLLNVAL);
}

rigctld_server::~rigctld_server() {
    delete[] pfds;
}
