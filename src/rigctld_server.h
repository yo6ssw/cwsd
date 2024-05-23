#ifndef CWSD_RIGCTLD_SERVER_H
#define CWSD_RIGCTLD_SERVER_H

#include <string>
#include <cstdint>
#include <hamlib/rig.h>
#include <map>
#include <atomic>
#include <thread>
#include <memory>
#include <sys/poll.h>

struct rigctld_client {
    int fd;
    std::string read_buffer;
    std::string write_buffer;
    bool is_running{true};
};

class rigctld_server {
public:

    rigctld_server(std::string device, int model, uint16_t listen_port);
    ~rigctld_server();

    void update();
    void work();
    void on_rig_disconnect();

    void stop();
private:

    bool open_rig();

    bool interpret_command(std::string &command, int client_fd);
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
    void update_poll_descriptors();
    int accept_client();
    bool read_from_client(rigctld_client &client);
    bool write_from_client(rigctld_client &client);
    void close_client(rigctld_client &client);
    bool poll_declares_error(short events);

    pollfd* pfds = nullptr;
    size_t pfd_nr;
    void update_poll_flags();
};

#endif
