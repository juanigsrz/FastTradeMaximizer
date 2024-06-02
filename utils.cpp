#include <locale>
#include <codecvt>
#include <string>

namespace utils{

size_t utf8_length(const string& str){
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(str).length();
}

} // namespace utils
