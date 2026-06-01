#include <thread>
#include <sys/socket.h>
#include "rigctld_server.h"
#include <fcntl.h>
#include <netinet/in.h>
#include "libs/easylogging++.h"
#include "string_util.h"
#include <poll.h>
#include <arpa/inet.h>
#include <hamlib/rig.h>

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

    LOG(DEBUG) << "setting rigctld listener to nonblocking";
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

//    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout)) < 0) {
//        throw std::runtime_error("failed to set read timeout");
//    }

    LOG(DEBUG) << "setting rigctld listener to reuse addr";
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
    LOG(INFO) << "rigctld listening on port " << listen_port;
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

        auto num_events = poll(pfds, pfd_nr, 1000); // timeout in ms
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

            for (auto &r: fds_to_remove) {
                this->close_client(clients[r]);
                clients.erase(r);
            }

            if (fd_to_add > 0 || !fds_to_remove.empty()) {
                update_poll_descriptors();
            }

            this->update_poll_flags();
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
        LOG(DEBUG) << "rig_init returned null";
        throw std::runtime_error("unknown rig num");
    }
    strncpy(rig->state.rigport.pathname, device.c_str(), FILPATHLEN - 1);

    int result;

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


bool rigctld_server::interpret_command(std::string &command, int client_fd) {
    auto cmd = trim(command);
    LOG(DEBUG) << "[c:" << client_fd << "] >> " << cmd;

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
	send_response_to_client("", client_fd); // yes, it needs an extra LF
    } else if (cmd == "\\dump_state") {
        send_response_to_client(dump_state(), client_fd);
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
    } else if (starts_with(cmd, "l ")) {
        send_response_to_client("42", client_fd);
    } else if (cmd == "q") {
        LOG(INFO) << "[c:" << client_fd << "] quit.";
        return false;
    } else if (cmd == "j") {
       shortfreq_t rit = 0;
       rig_get_rit(rig, RIG_VFO_CURR, &rit);
       send_response_to_client(std::to_string(rit), client_fd);
    } else if (starts_with(cmd, "l ")) {
       send_response_to_client("28", client_fd);
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

void rigctld_server::send_response_to_client(const std::string &response, int fd) {
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
    if (nullptr != pfds) {
        delete[] pfds;
    }


    pfds = new pollfd[clients.size() + 1];
    pfds[0].fd = server_fd;
    pfds[0].events = POLLIN;

    int index = 1;
    for (auto &c: clients) {
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

    int client_fd = accept(server_fd, (sockaddr *) &sa, &sa_size);
    if (client_fd < 0) {
        LOG(ERROR) << "rigctld accept() on rigctl listener returned " << client_fd;
    } else {
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN);

        fcntl(client_fd, F_SETFL, O_NONBLOCK);

        LOG(INFO) << "[c:" << client_fd << "] rigctld client connected from " << str << ":" << sa.sin_port;
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

    auto bytes_read = recv(client.fd, (char *) &msg, sizeof(msg) - 1, 0);
    if (bytes_read < 0) {
        LOG(WARNING) << "[c:" << client.fd << "] recv() error (errno=" << errno << ")";
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
            if (!interpret_command(cmd, client.fd)) {
                return false;
            }
            client.read_buffer.erase(0, cmd.size() + 1);
        }
    }

    return true;
}

bool rigctld_server::write_from_client(rigctld_client &client) {
    if (client.write_buffer.empty()) return true;

    auto written_bytes = write(client.fd, client.write_buffer.c_str(), client.write_buffer.size());
    if (written_bytes < 0) {
        LOG(ERROR) << "[c:" << client.fd << "] error writing";
        return false;
    }

    client.write_buffer.erase(0, written_bytes);
    return true;
}

void rigctld_server::close_client(rigctld_client &client) {
    LOG(INFO) << "rigctld closing client " << client.fd;
    shutdown(client.fd, SHUT_RDWR);
    close(client.fd);
}

bool rigctld_server::poll_declares_error(short events) {
    return events & (POLLERR | POLLHUP | POLLNVAL);
}

rigctld_server::~rigctld_server() {
    delete[] pfds;
}

void rigctld_server::update_poll_flags() {
    auto index = 1;
    for (auto &c: clients) {
        if (c.second.write_buffer.empty()) {
            pfds[index].events &= ~POLLOUT;
        } else {
            pfds[index].events |= POLLOUT;
        }
        index++;
    }
}

//int rig_sprintf_agc_levels(RIG *rig, char *str, int lenstr) {
//    auto priv_caps = (const struct icom_priv_caps *) rig->caps->priv;
//
//    int len = 0;
//    int i;
//    char tmpbuf[256];
//
//    str[len] = 0;
//
//    if (priv_caps && RIG_BACKEND_NUM(rig->caps->rig_model) == RIG_ICOM && priv_caps->agc_levels_present) {
//        for (i = 0; i <= HAMLIB_MAX_AGC_LEVELS && priv_caps->agc_levels[i].level != RIG_AGC_LAST; i++) {
//            if (strlen(str) > 0) { strcat(str, " "); }
//
//            snprintf(tmpbuf, sizeof(tmpbuf), "%d=%s",
//                     priv_caps->agc_levels[i].icom_level,
//                     rig_stragclevel(priv_caps->agc_levels[i].level));
//
//            if (strlen(str) + strlen(tmpbuf) < lenstr - 1) {
//                strncat(str, tmpbuf, lenstr - 1);
//            } else {
//                rig_debug(RIG_DEBUG_ERR, "%s: buffer overrun!!  len=%d > maxlen=%d\n",
//                          __func__, (int) (strlen(str) + strlen(tmpbuf)), lenstr - 1);
//            }
//        }
//    } else {
//        for (i = 0; i < HAMLIB_MAX_AGC_LEVELS && i < rig->caps->agc_level_count; i++) {
//            if (strlen(str) > 0) { strcat(str, " "); }
//
//            snprintf(tmpbuf, sizeof(tmpbuf), "%d=%s",
//                     rig->caps->agc_levels[i],
//                     rig_stragclevel(rig->caps->agc_levels[i]));
//            if (strlen(str) + strlen(tmpbuf) < lenstr - 1) {
//                strncat(str, tmpbuf, lenstr - 1);
//            } else {
//                rig_debug(RIG_DEBUG_ERR, "%s: buffer overrun!!  len=%d > maxlen=%d\n",
//                          __func__, (int) (strlen(str) + strlen(tmpbuf)), lenstr - 1);
//            }
//        }
//    }
//
//    return strlen(str);
//}

std::string rigctld_server::dump_state() {
    // What follows is a 1 to 1 translation from HamLib commit b4ec8a42
    std::stringstream result;
    const int RIGCTLD_PROT_VER = 1;
    result << std::to_string(RIGCTLD_PROT_VER) << "\n";
    result << std::to_string(rig->caps->rig_model) << "\n";

    result << "0\n"; // deprecated itu_region

    struct rig_state *rs = &rig->state;
    for (auto i = 0; i < HAMLIB_FRQRANGESIZ && !RIG_IS_FRNG_END(rs->rx_range_list[i]); i++) {
        result << format(
                "%lf %lf 0x%lld %d %d 0x%x 0x%x\n",
                rs->rx_range_list[i].startf,
                rs->rx_range_list[i].endf,
                rs->rx_range_list[i].modes,
                rs->rx_range_list[i].low_power,
                rs->rx_range_list[i].high_power,
                rs->rx_range_list[i].vfo,
                rs->rx_range_list[i].ant);
    }

    result << format("0 0 0 0 0 0 0\n");

    for (auto i = 0; i < HAMLIB_FRQRANGESIZ && !RIG_IS_FRNG_END(rs->tx_range_list[i]); i++) {
        result << format(
                "%lf %lf 0x%lld %d %d 0x%x 0x%x\n",
                rs->tx_range_list[i].startf,
                rs->tx_range_list[i].endf,
                rs->tx_range_list[i].modes,
                rs->tx_range_list[i].low_power,
                rs->tx_range_list[i].high_power,
                rs->tx_range_list[i].vfo,
                rs->tx_range_list[i].ant);
    }

    result << format("0 0 0 0 0 0 0\n");

    for (auto i = 0; i < HAMLIB_TSLSTSIZ && !RIG_IS_TS_END(rs->tuning_steps[i]); i++) {
        result << format("0x%lld %ld\n",
                         rs->tuning_steps[i].modes,
                         rs->tuning_steps[i].ts);
    }

    result << format("0 0\n");

    for (auto i = 0; i < HAMLIB_FLTLSTSIZ && !RIG_IS_FLT_END(rs->filters[i]); i++) {
        result << format("0x%lld %ld\n",
                         rs->filters[i].modes,
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

    result << format("0x%lld\n", rs->has_get_func);
    result << format("0x%lld\n", rs->has_set_func);
    result << format("0x%lld\n", rs->has_get_level);
    result << format("0x%lld\n", rs->has_set_level);
    result << format("0x%lld\n", rs->has_get_parm);
    result << format("0x%lld\n", rs->has_set_parm);

    // TODO: see how to properly do this
    static int chk_vfo_executed = 1;

    // for 3.3 compatiblility
    if (chk_vfo_executed) {
        result << format("vfo_ops=0x%x\n", rig->caps->vfo_ops);
        result << format("ptt_type=0x%x\n",
                         rig->state.pttport.type.ptt);
        result << format("targetable_vfo=0x%x\n", rig->caps->targetable_vfo);
        result << format("has_set_vfo=%d\n", rig->caps->set_vfo != NULL);
        result << format("has_get_vfo=%d\n", rig->caps->get_vfo != NULL);
        result << format("has_set_freq=%d\n", rig->caps->set_freq != NULL);
        result << format("has_get_freq=%d\n", rig->caps->get_freq != NULL);
        result << format("has_set_conf=%d\n", rig->caps->set_conf != NULL);
        result << format("has_get_conf=%d\n", rig->caps->get_conf != NULL);

        // for the future
//        fprintf(fout, "has_set_trn=%d\n", rig->caps->set_trn != NULL);
//        fprintf(fout, "has_get_trn=%d\n", rig->caps->get_trn != NULL);
        result << format("has_power2mW=%d\n", rig->caps->power2mW != NULL);
        result << format("has_mW2power=%d\n", rig->caps->mW2power != NULL);
        result << format("has_get_ant=%d\n", rig->caps->get_ant != NULL);
        result << format("has_set_ant=%d\n", rig->caps->set_ant != NULL);
        result << format("timeout=%d\n", rig->caps->timeout);
        result << format("rig_model=%d\n", rig->caps->rig_model);
        result << format("rigctld_version=%s\n", hamlib_version2);


        // TODO: this is hardcoded!!!!!!!!! fix rig_sprintf_agc_levels() before
//        rig_sprintf_agc_levels(rig, buf, sizeof(buf));
//        result << format("agc_levels=%s\n", buf);
        result << format("agc_levels=0=OFF 1=FAST 2=MEDIUM 3=SLOW\n");

        if (rig->caps->ctcss_list) {
            result << format("ctcss_list=");

            for (auto i = 0; i < CTCSS_LIST_SIZE && rig->caps->ctcss_list[i] != 0; i++) {
                result << format(" %u.%1u",
                                 rig->caps->ctcss_list[i] / 10, rig->caps->ctcss_list[i] % 10);
            }

            result << format("\n");
        }

        if (rig->caps->dcs_list) {
            result << format("dcs_list=");

            for (auto i = 0; i < DCS_LIST_SIZE && rig->caps->dcs_list[i] != 0; i++) {
                result << format(" %u",
                                 rig->caps->dcs_list[i]);
            }

            result << format("\n");
        }


        result << format("level_gran=");

        for (auto i = 0; i < RIG_SETTING_MAX; ++i) {
            if (RIG_LEVEL_IS_FLOAT(i)) {
                result << format("%d=%g,%g,%g;", i, rig->state.level_gran[i].min.f,
                                 rig->state.level_gran[i].max.f, rig->state.level_gran[i].step.f);
            } else {
                result << format("%d=%d,%d,%d;", i, rig->state.level_gran[i].min.i,
                                 rig->state.level_gran[i].max.i, rig->state.level_gran[i].step.i);
            }
        }

        result << format("\nparm_gran=");

        for (auto i = 0; i < RIG_SETTING_MAX; ++i) {
            if (RIG_LEVEL_IS_FLOAT(i)) {
                result << format("%d=%g,%g,%g;", i, rig->state.parm_gran[i].min.f,
                                 rig->state.parm_gran[i].max.f, rig->state.parm_gran[i].step.f);
            } else {
                result << format("%d=%d,%d,%d;", i, rig->state.level_gran[i].min.i,
                                 rig->state.level_gran[i].max.i, rig->state.level_gran[i].step.i);
            }
        }

        result << format("\n");

//        rig->state = rig->caps->rig_model;
        result << format("rig_model=%d\n", rig->caps->rig_model);
        result << format("hamlib_version=%s\n", hamlib_version2);
        result << format("done");
    }

    return result.str();
}
