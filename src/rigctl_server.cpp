//#include <stdexcept>
//#include "rigctl_server.h"
//#include <iostream>
//#include <sys/socket.h>
//#include <stdexcept>
//#include <netinet/in.h>
//#include <cstring>
//#include <iostream>
//#include <iomanip>
//#include <unistd.h>
//#include <map>
//#include <fcntl.h>
//#include <poll.h>
//#include "string_util.h"
//
//#define ELPP_SYSLOG
//
//#include <easylogging++.h>
//
//rigctl_server::rigctl_server() {
//    const char SERIAL_PORT[] = "/dev/icom7300";
//    rig_model = 3073;
////    detect_rig_model(SERIAL_PORT);
//    open_rig(SERIAL_PORT);
//    config_rig();
////    rig_parse_func()
//    worker = std::thread(&rigctl_server::work, this);
//}
//
//
//void rigctl_server::work() {
//    int server_fd;
//    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//        SYSLOG(ERROR) << "failed to create rigctl listener socket" << std::endl;
//    }
//
//    SYSLOG(INFO) << "setting rigctl listener to nonblocking" << std::endl;
//    fcntl(server_fd, F_SETFL, O_NONBLOCK);
//
////    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout)) < 0) {
////        throw std::runtime_error("failed to set read timeout");
////    }
//
//    SYSLOG(INFO) << "setting rigctl listener to reuse addr" << std::endl;
//    int reuseOption = 1;
//    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseOption, sizeof(reuseOption));
//
//    sockaddr_in serv_addr{};
//    memset(&serv_addr, 0, sizeof(serv_addr));
//
//    serv_addr.sin_family = AF_INET; // IPv4
//    serv_addr.sin_addr.s_addr = INADDR_ANY;
//    serv_addr.sin_port = htons(4532);
//
//    if (bind(server_fd, reinterpret_cast<const struct sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0) {
//        SYSLOG(ERROR) << "failed to bind rigctl listener" << std::endl;
//        throw std::runtime_error("bind failed");
//    }
//
//    listen(server_fd, 5);
//
//    sockaddr_in new_sock_addr{};
//    socklen_t new_sock_addr_size = sizeof(new_sock_addr);
//
//    struct pollfd pfds[1];
//    pfds[0].fd = server_fd;
//    pfds[0].events = POLLIN;
//
//    while (true) {
//        auto num_events = poll(pfds, 1, 100); // timeout in ms
//        if (num_events > 0 && pfds[0].revents & POLLIN) {
//            int client_fd = accept(server_fd, (sockaddr *) &new_sock_addr, &new_sock_addr_size);
//            if (client_fd < 0) {
//                SYSLOG(ERROR) << "accept() on rigctl listener returned " << client_fd << std::endl;
//            } else {
//                SYSLOG(INFO) << "rigctl client connected from " << new_sock_addr.sin_addr.s_addr << std::endl;
//                client_handlers[client_fd] = std::thread(&rigctl_server::client_handler, this, client_fd);
//            }
//        }
//    }
//}
//
//
//void rigctl_server::client_handler(int client_fd) {
//    char msg[1500];
//    while (rig_connected.load()) {
//        memset(&msg, 0, sizeof(msg));
//        auto bytes_read = recv(client_fd, (char *) &msg, sizeof(msg), 0);
//        if (bytes_read > 0) {
//            msg[bytes_read] = '\0';
//            auto command = std::string(msg, strlen(msg));
//            interpret_command(command, client_fd);
//        }
//    }
//    SYSLOG(INFO) << "closing client " << client_fd << std::endl;
//    shutdown(client_fd, SHUT_RDWR);
//    close(client_fd);
//}
//
//static int debug_callback(enum rig_debug_level_e level, rig_ptr_t user_data, const char *message, va_list args) {
//    auto disconnected = string_contains(message, "read failed");
//    if (disconnected) {
//        rigctl_server *self = reinterpret_cast<rigctl_server *>(user_data);
//        self->on_rig_disconnect();
//    }
//    return 0;
//}
//
//void rigctl_server::config_rig() const {
//    int result;
//    result = rig_set_conf(rig, rig_token_lookup(rig, "rts_state"), "OFF");
//    if (result != RIG_OK) {
//        fprintf(stderr, "Config parameter error: %s\n", rigerror(result));
//        exit(2);
//    }
//    result = rig_set_conf(rig, rig_token_lookup(rig, "dtr_state"), "OFF");
//    if (result != RIG_OK) {
//        fprintf(stderr, "Config parameter error: %s\n", rigerror(result));
//        exit(2);
//    }
//}
//
//bool rigctl_server::open_rig(const char *serial_port) {
//    rig = rig_init(rig_model);
//    if (!rig) {
//        throw std::runtime_error("Unknown rig num: ");
//    }
//    strncpy(rig->state.rigport.pathname, serial_port, FILPATHLEN - 1);
//
//    int result = rig_open(rig);
//    if (result != RIG_OK) {
//        printf("rig_open: error = %s\n", rigerror(result));
//        throw std::runtime_error("Failed to open rig");
//    }
//
//    rig_connected.store(true);
//
//    rig_set_debug(RIG_DEBUG_ERR);
//    rig_set_debug_callback(debug_callback, this);
//    return true;
//}
//
//void rigctl_server::detect_rig_model(const char *serial_port) {
//    rig_load_all_backends();
//
//    hamlib_port_t port;
//    port.type.rig = RIG_PORT_SERIAL;
//    port.parm.serial.rate = 115200;
//    port.parm.serial.data_bits = 8;
//    port.parm.serial.stop_bits = 1;
//    port.parm.serial.parity = RIG_PARITY_NONE;
//    port.parm.serial.handshake = RIG_HANDSHAKE_NONE;
//    strncpy(port.pathname, serial_port, FILPATHLEN - 1);
//
//    rig_model = rig_probe(&port);
//}
//
//rigctl_server::~rigctl_server() {
//    rig_close(rig);
//    rig_cleanup(rig);
//}
//
//void rigctl_server::interpret_command(std::string &command, int client_fd) {
//    auto cmd = trim(command);
//    SYSLOG(DEBUG) << "[c:" << client_fd << "] >> " << cmd;
//
//    if (cmd == "\\get_powerstat") {
//        powerstat_t status;
//        rig_get_powerstat(rig, &status);
//        send_response_to_client(to_client(status), client_fd);
//    } else if (cmd == "\\chk_vfo") {
//        // TODO: don't hardcode this
//        send_response_to_client("0", client_fd);
//    } else if (cmd == "\\get_lock_mode") {
//        int lock;
//        rig_get_lock_mode(rig, &lock);
//        send_response_to_client(std::to_string(lock), client_fd);
//    } else if (cmd == "\\dump_state") {
//        // TODO: don't hardcode this
//        send_response_to_client(
//                "1\n3073\n0\n30000.000000 74800000.000000 0x401dbf -1 -1 0x10000003 0x0\n0 0 0 0 0 0 0\n1810000.000000 2000000.000000 0x1be 2000 100000 0x10000003 0x1\n3500000.000000 3800000.000000 0x1be 2000 100000 0x10000003 0x1\n7000000.000000 7200000.000000 0x1be 2000 100000 0x10000003 0x1\n10100000.000000 10150000.000000 0x1be 2000 100000 0x10000003 0x1\n14000000.000000 14350000.000000 0x1be 2000 100000 0x10000003 0x1\n18068000.000000 18168000.000000 0x1be 2000 100000 0x10000003 0x1\n21000000.000000 21450000.000000 0x1be 2000 100000 0x10000003 0x1\n24890000.000000 24990000.000000 0x1be 2000 100000 0x10000003 0x1\n28000000.000000 29700000.000000 0x1be 2000 100000 0x10000003 0x1\n5351500.000000 5366500.000000 0x1be 2000 100000 0x10000003 0x1\n50000000.000000 54000000.000000 0x1be 2000 100000 0x10000003 0x1\n70000000.000000 70500000.000000 0x1be 2000 50000 0x10000003 0x1\n1810000.000000 2000000.000000 0x400001 1000 25000 0x10000003 0x1\n3500000.000000 3800000.000000 0x400001 1000 25000 0x10000003 0x1\n7000000.000000 7200000.000000 0x400001 1000 25000 0x10000003 0x1\n10100000.000000 10150000.000000 0x400001 1000 25000 0x10000003 0x1\n14000000.000000 14350000.000000 0x400001 1000 25000 0x10000003 0x1\n18068000.000000 18168000.000000 0x400001 1000 25000 0x10000003 0x1\n21000000.000000 21450000.000000 0x400001 1000 25000 0x10000003 0x1\n24890000.000000 24990000.000000 0x400001 1000 25000 0x10000003 0x1\n28000000.000000 29700000.000000 0x400001 1000 25000 0x10000003 0x1\n5351500.000000 5366500.000000 0x400001 1000 25000 0x10000003 0x1\n50000000.000000 54000000.000000 0x400001 1000 25000 0x10000003 0x1\n70000000.000000 70500000.000000 0x400001 1000 12500 0x10000003 0x1\n0 0 0 0 0 0 0\n0x401dbf 1\n0x401dbf 1000\n0x401dbf 5000\n0x401dbf 9000\n0x401dbf 10000\n0x401dbf 12500\n0x401dbf 20000\n0x401dbf 25000\n0 0\n0xc0c 2400\n0xc0c 1800\n0xc0c 3000\n0x192 500\n0x192 250\n0x82 1200\n0x110 2400\n0x400001 6000\n0x400001 3000\n0x400001 9000\n0x1020 10000\n0x1020 7000\n0x1020 15000\n0 0\n9999\n9999\n0\n0\n1 2 \n20 \n0xfc00c90133fe\n0xfc00c90133fe\n0xc7fff74677f3f\n0xc7f7000677f3f\n0x35\n0x35\nvfo_ops=0x81f\nptt_type=0x1\ntargetable_vfo=0x3\nhas_set_vfo=1\nhas_get_vfo=0\nhas_set_freq=1\nhas_get_freq=1\nhas_set_conf=1\nhas_get_conf=1\nhas_power2mW=1\nhas_mW2power=1\ntimeout=1000\nrig_model=3073\nrigctld_version=Hamlib 4.5.5 Apr 05 11:43:08Z 2023 SHA=6eecd3\nagc_levels=0=OFF 1=FAST 2=MEDIUM 3=SLOW\nctcss_list= 60.0 67.0 69.3 71.9 74.4 77.0 79.7 82.5 85.4 88.5 91.5 94.8 97.4 100.0 103.5 107.2 110.9 114.8 118.8 120.0 123.0 127.3 131.8 136.5 141.3 146.2 151.4 156.7 159.8 162.2 165.5 167.9 171.3 173.8 177.3 179.9 183.5 186.2 189.9 192.8 196.6 199.5 203.5 206.5 210.7 218.1 225.7 229.1 233.6 241.8 250.3 254.1\ndone",
//                client_fd);
//    } else if (cmd == "f") {
//        freq_t freq;
//        rig_get_freq(rig, RIG_VFO_CURR, &freq);
//        send_response_to_client(std::to_string(freq), client_fd);
//    } else if (cmd == "s") {
//        split_t split = RIG_SPLIT_ON;
//        vfo_t txvfo = 0;
//        rig_get_split_vfo(rig, RIG_VFO_CURR, &split, &txvfo);
//        std::stringstream ss;
//        ss << split_to_string(split) << "\n" << vfo_to_string(txvfo);
//        send_response_to_client(ss.str(), client_fd);
//    } else if (cmd == "t") {
//        ptt_t ptt;
//        rig_get_ptt(rig, RIG_VFO_CURR, &ptt);
//        send_response_to_client(std::to_string(ptt), client_fd);
//    } else if (starts_with(cmd, "T ")) {
//        ptt_t ptt = static_cast<ptt_t>(std::stoi(&cmd.c_str()[2]));
//        rig_set_ptt(rig, RIG_VFO_CURR, ptt);
//        send_response_to_client("RPRT 0", client_fd);
//    } else if (starts_with(cmd, "V ")) {
//        rig_set_vfo(rig, vfo_from_string(&cmd.c_str()[2]));
//        send_response_to_client("RPRT 0", client_fd);
//    } else if (cmd == "m") {
//        rmode_t mode = 0;
//        pbwidth_t width = 0;
//        rig_get_mode(rig, RIG_VFO_CURR, &mode, &width);
//        std::stringstream ss;
//        ss << mode_to_string(mode) << "\n" << width;
//        send_response_to_client(ss.str(), client_fd);
//    } else if (starts_with(cmd, "M ")) {
//        // M PKTUSB -1
//        auto pieces = split_string(cmd, " ");
//        auto pbwidth = std::stoi(pieces[2]);
//        rig_set_mode(rig, RIG_VFO_CURR, mode_from_string(pieces[1].c_str()), pbwidth);
//        send_response_to_client("RPRT 0", client_fd);
//    } else if (starts_with(cmd, "F ")) {
//        freq_t freq = std::stod(&cmd.c_str()[2]);
//        rig_set_freq(rig, RIG_VFO_CURR, freq);
//        send_response_to_client("RPRT 0", client_fd);
//    } else {
//        std::cerr << "Unhandled command [" << cmd << "]" << std::endl;
//    }
//}
//
//std::string rigctl_server::to_client(powerstat_t status) {
//    std::string result;
//    result += char('0' + status);
//    return result;
//}
//
//void rigctl_server::send_response_to_client(const std::string& response, int fd) {
//    std::stringstream ss;
//    ss << response << '\n';
//    auto full_response = ss.str();
//
//    SYSLOG(DEBUG) << "[c:" << fd << "] << " << trim(full_response);
//
//    size_t written_bytes = 0;
//    size_t to_write = full_response.size();
//    while (to_write > 0) {
//        auto wb = write(fd, &full_response.c_str()[written_bytes], to_write);
//        if (wb > 0) {
//            written_bytes += wb;
//            to_write -= wb;
//        }
//    }
//}
//
//std::string rigctl_server::vfo_to_string(vfo_t vfo) {
//    switch (vfo) {
//        case RIG_VFO_A:
//            return "VFOA";
//        case RIG_VFO_B:
//            return "VFOB";
//        case RIG_VFO_C:
//            return "VFOC";
//        default:
//            return "UNKNOWN";
//    }
//}
//
//vfo_t rigctl_server::vfo_from_string(const char *vfo_name) {
//    if (strcmp(vfo_name, "VFOA") == 0) {
//        return RIG_VFO_A;
//    } else if (strcmp(vfo_name, "VFOB") == 0) {
//        return RIG_VFO_B;
//    } else if (strcmp(vfo_name, "VFOC") == 0) {
//        return RIG_VFO_C;
//    } else {
//        return RIG_VFO_CURR;
//    }
//}
//
//const char *rigctl_server::mode_to_string(rmode_t mode) {
//    switch (mode) {
//        case RIG_MODE_USB:
//            return "USB";
//        case RIG_MODE_LSB:
//            return "LSB";
//        case RIG_MODE_CW:
//            return "CW";
//        case RIG_MODE_RTTY:
//            return "RTTY";
//        case RIG_MODE_RTTYR:
//            return "RTTYR";
//        case RIG_MODE_AM:
//            return "AM";
//        case RIG_MODE_FM:
//            return "FM";
//        case RIG_MODE_PKTLSB:
//            return "PKTLSB";
//        case RIG_MODE_PKTUSB:
//            return "PKTUSB";
//        case RIG_MODE_PKTFM:
//            return "PKTFM";
//        default:
//            return "UNKNOWN";
//    }
//}
//
//rmode_t rigctl_server::mode_from_string(const char *name) {
//    if (strcmp(name, "USB") == 0)
//        return RIG_MODE_USB;
//    else if (strcmp(name, "LSB") == 0)
//        return RIG_MODE_LSB;
//    else if (strcmp(name, "CW") == 0)
//        return RIG_MODE_CW;
//    else if (strcmp(name, "RTTY") == 0)
//        return RIG_MODE_RTTY;
//    else if (strcmp(name, "RTTYR") == 0)
//        return RIG_MODE_RTTYR;
//    else if (strcmp(name, "AM") == 0)
//        return RIG_MODE_AM;
//    else if (strcmp(name, "FM") == 0)
//        return RIG_MODE_FM;
//    else if (strcmp(name, "PKTLSB") == 0)
//        return RIG_MODE_PKTLSB;
//    else if (strcmp(name, "PKTUSB") == 0)
//        return RIG_MODE_PKTUSB;
//    else if (strcmp(name, "PKTFM") == 0)
//        return RIG_MODE_PKTFM;
//    else
//        return RIG_MODE_ALL;
//}
//
//const char *rigctl_server::split_to_string(split_t split) {
//    if (split == RIG_SPLIT_ON)
//        return "1";
//    else
//        return "0";
//}
//
//void rigctl_server::on_rig_disconnect() {
//    bool expected = true;
//    if (rig_connected.compare_exchange_strong(expected, false)) {
//        SYSLOG(INFO) << "rig disconnected" << std::endl;
//        client_handlers.clear();
//    }
//}
