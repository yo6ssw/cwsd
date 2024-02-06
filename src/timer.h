#ifndef CWSD_TIMER_H
#define CWSD_TIMER_H

#include <cstdint>
#include <chrono>

class timer {
public:
    timer();
    uint32_t ellapsed_ms() const;
private:
    std::chrono::time_point<std::chrono::system_clock> start;
};


#endif //CWSD_TIMER_H
