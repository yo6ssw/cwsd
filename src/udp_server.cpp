// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

//
// Created by benny on 05/02/24.
//

#include <sys/socket.h>
#include <stdexcept>
#include <netinet/in.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include "udp_server.h"
#include <unistd.h>
#include <libs/easylogging++.h>

udp_server::udp_server(uint16_t port, timeval read_timeout) {
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        throw std::runtime_error("socket creation failed");
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout)) < 0) {
        throw std::runtime_error("failed to set read timeout");
    }

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if (bind(sockfd, reinterpret_cast<const struct sockaddr *>(&servaddr), sizeof(servaddr)) < 0) {
        throw std::runtime_error("bind failed");
    }

    LOG(INFO) << "cwdaemon listening on port " << port;
}

std::string msg_for_log(std::vector<uint8_t> message) {
    std::stringstream ss;
    for (auto i = 0; i < message.size() - 1; i++) {
        auto &c = message[i];
        if (std::isprint(c)) {
            ss << c;
        } else {
            ss << "(0x" << std::setw(2) << std::setfill('0') << std::uppercase << std::hex << (int) c << ")"
               << std::dec;
        }
    }

    return ss.str();
}

bool udp_server::receive() {
    sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t client_addr_len = sizeof(client_addr);

    auto n = recvfrom(sockfd,
                      buffer,
                      1024,
                      0,
                      reinterpret_cast<struct sockaddr *>(&client_addr),
                      &client_addr_len);
    if (n < 0 && errno != EAGAIN) {
//        throw std::runtime_error("recvfrom failed with errno " + errno);
        return false;
    }

    if (n > 0) {
        while (n > 1 && (buffer[n - 1] == '\n' || buffer[n - 1] == '\r' || buffer[n - 1] == ' ')) {
            n--;
        }

        buffer[n] = 0;
        message = std::vector<uint8_t>(buffer, buffer + n + 1);
        LOG(DEBUG) << "cwdaemon client sent [" << msg_for_log(message) << "]";
        return true;
    } else {
        return false;
    }
}

std::vector<uint8_t> udp_server::last_message() {
    return message;
}

udp_server::~udp_server() {
    if (sockfd > 0) {
        close(sockfd);
    }
}
