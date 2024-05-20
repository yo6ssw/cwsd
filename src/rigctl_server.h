//#ifndef CWSD_RIGCTL_SERVER_H
//#define CWSD_RIGCTL_SERVER_H
//
//#include <hamlib/rig.h>
//#include <thread>
//#include <atomic>
//#include <map>
//
//class rigctl_server {
//public:
//    rigctl_server();
//    ~rigctl_server();
//
//    void on_rig_disconnect();
//
//private:
//    RIG *rig;
//    rig_model_t rig_model;
//    void detect_rig_model(const char *serial_port);
//
//    bool open_rig(const char *serial_port);
//    void config_rig() const;
//
//    void work();
//    std::thread worker;
//    void client_handler(int client_fd);
//
//    void interpret_command(std::string &command, int client_fd);
//    static std::string to_client(powerstat_t status);
//    static void send_response_to_client(const std::string &response, int fd);
//
//    static rmode_t mode_from_string(const char *name);
//    static const char *mode_to_string(rmode_t mode);
//    static vfo_t vfo_from_string(const char *vfo_name);
//    static std::string vfo_to_string(vfo_t vfo);
//    static const char *split_to_string(split_t split);
//
//    std::map<int, std::thread> client_handlers;
//    std::atomic<bool> rig_connected{false};
//};
//
//
//#endif //CWSD_RIGCTL_SERVER_H
