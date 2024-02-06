#include "cw_daemon.h"

#include <iostream>
#include <string>

std::vector<uint8_t> cw_daemon::to_winkeyer(std::vector<uint8_t> input, uint8_t initial_speed) {

    auto current_speed = initial_speed;
    auto buffer = reinterpret_cast<const char*>(input.data());

    std::vector<uint8_t> result;
    bool is_gap = false;
    std::cout << "Initial speed is " << (int)current_speed << std::endl;
    if (buffer[0] != 27) {
        auto ptr = buffer;
        while (*ptr > 0) {
            switch (*ptr) {
                case '-': {
                    int times = 0;
                    for (; *ptr == '-'; ptr++, times++);
                    result.push_back(0x1c);
                    current_speed -= 2 * times;
                    std::cout << "- pushing buffered speed to " << (int) current_speed << std::endl;
                    result.push_back(current_speed);
                    // keyer::winkeyer_data(current_speed);
                    break;
                }
                case '+': {
                    int times = 0;
                    for (; *ptr == '+'; ptr++, times++);
                    result.push_back(0x1c);
                    current_speed += 2 * times;
                    result.push_back(current_speed);
                    std::cout << "- pushing buffered speed to " << (int) current_speed << std::endl;

                    break;
                }
                case '~': {
                    is_gap = true;
                    *ptr++;
                    break;
                }
                default:
                    if (*ptr >= 32) {
                        // std::cout << "- pushing [" << *ptr << "]" << std::endl;

                        result.push_back(std::toupper(*ptr));
                        // keyer::winkeyer_data(*ptr);
                        if (is_gap) {
                            result.push_back('|');
                            result.push_back('|');
                            result.push_back('|');
                            result.push_back('|');
                            is_gap = false;
                        }
                    }
                    *ptr++;
                    break;
            }
        }
    } else {
        switch (buffer[1]) {
            case '2': {
                unsigned char speed = std::stoi(&buffer[2]);
                // std::cout << "- setting speed to " << static_cast<int>(speed) << std::endl;
                result.push_back(0x02);
                result.push_back(speed);
                break;
            }
            case '4': {
                result.push_back(0x0A);
                break;
            }
            case '7': {
                unsigned char weight = 50 + std::stoi(&buffer[2]);
                if (weight < 10) weight = 10;
                if (weight > 90) weight = 90;
                result.push_back(0x03);
                result.push_back(weight);
                break;
            }
            case 'c': {
                // immediate key on
                unsigned char seconds = std::stoi(&buffer[2]);
                result.push_back(0x0B);
                result.push_back(1);
                result.push_back(seconds);
                break;
            }
            default:
                break;
        }
    }

    // buffer original speed
    if (current_speed != initial_speed) {
        result.push_back(0x1C);
        result.push_back(initial_speed);
    }

    return result;
}

bool cw_daemon::is_tuning_command(std::vector<uint8_t> message) {
    return message.size() > 2 && message[0] == 0x0B && message[1] == 1;
}
