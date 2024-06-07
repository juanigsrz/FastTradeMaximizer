#pragma once

#include <locale>
#include <codecvt>
#include <string>
#include <chrono>

namespace utils{

std::wstring toWide(std::string s){
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wide = converter.from_bytes(s);
    return wide;
}


size_t utf8_length(std::string s){  return toWide(s).length(); }

class timer : std::chrono::high_resolution_clock {
    const time_point start_time;
public:
    timer(): start_time(now()) {}
    rep elapsed_time() const { return std::chrono::duration_cast<std::chrono::milliseconds>(now()-start_time).count(); }
};

void up(std::string& str){ transform(str.begin(), str.end(), str.begin(), ::toupper); }

} // namespace utils
