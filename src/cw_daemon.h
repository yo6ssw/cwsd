#ifndef CW_DAEMON_H
#define CW_DAEMON_H

#include <cstdint>
#include <vector>

class cw_daemon {
public:
    static std::vector<uint8_t> to_winkeyer(std::vector<uint8_t> input, uint8_t current_speed);
};



#endif //CW_DAEMON_H
