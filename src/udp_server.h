// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#ifndef CWSD_UDP_SERVER_H
#define CWSD_UDP_SERVER_H
#include <cstdint>
#include <cstdlib>
#include <vector>

class udp_server {
 public:
  udp_server(uint16_t port, timeval read_timeout);
  ~udp_server();

  bool receive();
  std::vector<uint8_t> last_message();

 private:
  int sockfd;
  char buffer[1024];
  std::vector<uint8_t> message;
};

#endif  // CWSD_UDP_SERVER_H
