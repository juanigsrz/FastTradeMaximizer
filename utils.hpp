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

namespace std {

string to_string(__int128_t x) {
    if (x == 0)
        return "0";
    __uint128_t k = x;
    if (k == (((__uint128_t)1) << 127))
        return "-170141183460469231731687303715884105728";
    string result;
    if (x < 0) {
        result += "-";
        x *= -1;
    }
    string t;
    while (x) {
        t.push_back('0' + x % 10);
        x /= 10;
    }
    reverse(t.begin(), t.end());
    return result + t;
}

ostream& operator<<(ostream& o, __int128_t x) { return o << to_string(x); }

} // namespace std