#ifndef STRING_UTIL_H_
#define STRING_UTIL_H_

#include <string>
#include <vector>

std::string &ltrim(std::string &str, const std::string &chars = "\t\n\v\f\r ") {
    str.erase(0, str.find_first_not_of(chars));
    return str;
}

std::string ltrim(std::string &&str, const std::string &chars = "\t\n\v\f\r ") {
    str.erase(0, str.find_first_not_of(chars));
    return str;
}

std::string &rtrim(std::string &str, const std::string &chars = "\t\n\v\f\r ") {
    str.erase(str.find_last_not_of(chars) + 1);
    return str;
}

std::string rtrim(std::string &&str, const std::string &chars = "\t\n\v\f\r ") {
    str.erase(str.find_last_not_of(chars) + 1);
    return str;
}

void trim(std::string &str, const std::string chars) {
    ltrim(rtrim(str, chars), chars);
}

std::string trim(const std::string str) {
        auto result = str;
        trim(result, "\t\n\v\f\r ");
        return result;
}

bool starts_with(std::string haystack, std::string needle) {
    return (haystack.rfind(needle, 0) == 0);
}

std::vector<std::string> split_string(const std::string &in, std::string delimiter) {
    std::vector<std::string> result{};
    size_t last{0}, next;
    while ((next = in.find(delimiter, last)) != std::string::npos) {
        result.push_back(in.substr(last, next - last));
        last = next + delimiter.size();
    }
    result.push_back(in.substr(last));
    return result;
}

bool string_contains(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}


#endif