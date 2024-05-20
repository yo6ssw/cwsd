//
// Created by benny on 14/05/24.
//

#ifndef CWSD_RIGCTLD_SERVER_H
#define CWSD_RIGCTLD_SERVER_H


#include <string>
#include <cstdint>
#include <hamlib/rig.h>
#include <map>
#include <atomic>
#include <thread>
#include <memory>

struct rigctld_client {
    int fd;
    std::string read_buffer;
    std::string write_buffer;
    std::shared_ptr<std::thread> worker;
    bool is_running{true};
};

class rigctld_server {
public:

    rigctld_server(std::string device, int model, uint16_t listen_port);

    void update();
    void work();
    void on_rig_disconnect();

    void stop();
private:

    void client_handler(int client_fd);
    bool open_rig();

    void interpret_command(std::string &command, int client_fd);
    static std::string to_client(powerstat_t status);
    void send_response_to_client(const std::string &response, int fd);
    static std::string vfo_to_string(vfo_t vfo);
    static vfo_t vfo_from_string(const char *vfo_name);
    static const char *mode_to_string(rmode_t mode);
    static rmode_t mode_from_string(const char *name);
    static const char *split_to_string(split_t split);

    RIG *rig = nullptr;
    rig_model_t rig_model;
    std::string device;
    uint16_t listen_port;
    std::thread worker;
    void start_listener();
    int server_fd;
    std::map<int, rigctld_client> clients;
    std::atomic<bool> rig_disconnected{true};
    std::atomic<bool> is_running;
};


#endif //CWSD_RIGCTLD_SERVER_H
