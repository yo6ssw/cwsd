#include <chrono>
#include <iostream>
#include "keyer/keyer.h"
#include <chrono>
#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>


typedef std::chrono::high_resolution_clock Clock;

struct key_interface : keyer::hw_interface {
    int fd;
    int key;
    int ptt;
    std::chrono::time_point<std::chrono::system_clock> start;

    key_interface(const char* device) {
        std::cout << "- opening device " << device << std::endl;
        fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "- failed to open " << device << std::endl;
            exit(1);
        }

        start = Clock::now();

        key = TIOCM_DTR;
        ptt = TIOCM_RTS;

        int m = 0;
        ioctl(fd, TIOCMGET, &m);

        on_key_up();
    }

    uint32_t current_ms() override {
        const auto now = Clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    }

    bool is_paddle_pressed(keyer::paddle_side side) override {
        return false;
    }

    void on_key_down() override {
        ioctl(fd, TIOCMBIS, &key);
    }

    void on_key_up() override {
        ioctl(fd, TIOCMBIC, &key);
    }
};


void udpServer() {
    constexpr uint16_t PORT = 6789;
    constexpr size_t MAXLINE = 1024;

    int sockfd;
    char buffer[MAXLINE];
    sockaddr_in servaddr, cliaddr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, reinterpret_cast<const struct sockaddr *>(&servaddr), sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "- server listening on port " << PORT << std::endl;

    socklen_t len = sizeof(cliaddr);
    while (true) {
        const int n = recvfrom(sockfd, buffer,
                               MAXLINE,
                               MSG_WAITALL,
                               reinterpret_cast<struct sockaddr *>(&cliaddr),
                               &len);
        buffer[n] = '\0';
        std::cout << "- client sent [" << buffer << "]" << std::endl;

        auto current_speed = static_cast<unsigned char>(keyer::get_speed());
        if (buffer[0] != 27) {
            for (int i = 0; buffer[i] != 0; i++) {
                int to_increase = 0;
                while (buffer[i] == '+') {
                    to_increase++;
                    i++;
                }
                if (to_increase > 0) {
                    keyer::winkeyer_data(0x1c);
                    current_speed += 2 * to_increase;
                    std::cout << "- pushing buffered speed to " << (int) current_speed << std::endl;
                    keyer::winkeyer_data(current_speed);
                }

                int to_decrease = 0;
                while (buffer[i] == '-') {
                    to_decrease++;
                    i++;
                }
                if (to_decrease > 0) {
                    keyer::winkeyer_data(0x1c);
                    current_speed -= 2 * to_decrease;
                    std::cout << "- pushing buffered speed to " << (int) current_speed << std::endl;
                    keyer::winkeyer_data(current_speed);
                }

                if (buffer[i] > 0) {
                    if (buffer[i] >= 32) {
                        std::cout << "- pushing [" << buffer[i] << "]" << std::endl;
                        keyer::winkeyer_data(buffer[i]);
                    }
                }
            }
        } else {
            switch (buffer[1]) {
                case '2': {
                    unsigned char speed = std::stoi(&buffer[2]);
                    std::cout << "- setting speed to " << static_cast<int>(speed) << std::endl;
                    keyer::winkeyer_data(0x02);
                    keyer::winkeyer_data(speed);
                    break;
                }
                case '4': {
                    keyer::winkeyer_data(0x0A);
                    break;
                }
                default:
                    break;
            }
        }
    }
}


void usage() {
    std::cerr << "Usage: ./cwkeyer /path/to/device" << std::endl;
    exit(1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
    }

    key_interface iface{argv[1]};
    keyer::init(&iface, 22050);

    std::thread worker(udpServer);
    while (true) {
        keyer::update();
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    return 0;
}
