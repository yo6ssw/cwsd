// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#ifndef STRING_UTIL_H_
#define STRING_UTIL_H_

#include <string>
#include <vector>

std::string& ltrim(std::string& str, const std::string& chars = "\t\n\v\f\r ") {
  str.erase(0, str.find_first_not_of(chars));
  return str;
}

std::string ltrim(std::string&& str, const std::string& chars = "\t\n\v\f\r ") {
  str.erase(0, str.find_first_not_of(chars));
  return str;
}

std::string& rtrim(std::string& str, const std::string& chars = "\t\n\v\f\r ") {
  str.erase(str.find_last_not_of(chars) + 1);
  return str;
}

std::string rtrim(std::string&& str, const std::string& chars = "\t\n\v\f\r ") {
  str.erase(str.find_last_not_of(chars) + 1);
  return str;
}

void trim(std::string& str, const std::string chars) {
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

void replace_all(std::string& str, const std::string& from,
                 const std::string& to) {
  if (from.empty()) return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();  // In case 'to' contains 'from', like replacing
                               // 'x' with 'yx'
  }
}

std::vector<std::string> split_string(const std::string& in,
                                      std::string delimiter) {
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

// https://codereview.stackexchange.com/questions/187183/create-a-c-string-using-printf-style-formatting
std::string format(const char* fmt, ...) {
  char buf[256];

  va_list args;
  va_start(args, fmt);
  const auto r = std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);

  if (r < 0)
    // conversion failed
    return {};

  const size_t len = r;
  if (len < sizeof buf)
    // we fit in the buffer
    return {buf, len};

#if __cplusplus >= 201703L
  // C++17: Create a string and write to its underlying array
  std::string s(len, '\0');
  va_start(args, fmt);
  std::vsnprintf(s.data(), len + 1, fmt, args);
  va_end(args);

  return s;
#else
  // C++11 or C++14: We need to allocate scratch memory
  auto vbuf = std::unique_ptr<char[]>(new char[len + 1]);
  va_start(args, fmt);
  std::vsnprintf(vbuf.get(), len + 1, fmt, args);
  va_end(args);

  return {vbuf.get(), len};
#endif
}

#endif