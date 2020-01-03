#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <cstdlib>
#include <experimental/filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define TARGET_OS_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Skip
#include <fcntl.h>
#include <io.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <shellapi.h>
#endif
#ifdef __linux__
#include <alsa/asoundlib.h>
#include <sys/ioctl.h>
#define PCM_DEVICE "default"
#endif

#define SKIP_MP4_ELEMENT_IDS 1
#define UKNOWN "unknown"

#ifndef GIT_BRANCH
#define GIT_BRANCH UKNOWN
#endif
#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH UKNOWN
#endif

#ifndef LOOPER_VERSION
#define LOOPER_VERSION UKNOWN
#endif

extern "C" {
#include <FLAC/all.h>
#include <alac/ALACBitUtilities.h>
#include <alac/ALACDecoder.h>
#include <alac/EndianPortable.h>
#include <libaiff/libaiff.h>
#include <mpg123.h>
#include <opus/opusfile.h>
#include <vorbis/vorbisfile.h>
}

#include <aacdecoder_lib.h>

namespace fs = std::experimental::filesystem;

#ifdef LOOPER_DEBUG
#define TRACE_INFO(info)                                                     \
  looper::Logging::TraceMessage::log(__FUNCTION__, info, __FILE__, __LINE__, \
                                     looper::LogLevel::INFO)
#define TRACE_ERROR(error)                                                    \
  looper::Logging::TraceMessage::log(__FUNCTION__, error, __FILE__, __LINE__, \
                                     looper::LogLevel::ERR)
#define TRACE_WARNING(warning)                                        \
  looper::Logging::TraceMessage::log(__FUNCTION__, warning, __FILE__, \
                                     __LINE__, looper::LogLevel::WARNING)
#define TRACE_SUCCESS(success)                                        \
  looper::Logging::TraceMessage::log(__FUNCTION__, success, __FILE__, \
                                     __LINE__, looper::LogLevel::SUCCESS)
#else
#define TRACE_INFO(info)
#define TRACE_ERROR(error)
#define TRACE_SUCCESS(success)
#define TRACE_WARNING(warning)
#endif

#define MAXBUFFERSIZE 1024
#define SWAP_UINT16(x) looper::Bytes::bswap_uint16(x)
#define SWAP_UINT32(x) looper::Bytes::bswap_uint32(x)
#define SWAP_UINT64(x) looper::Bytes::bswap_uint64(x)

namespace looper {

static const std::string EMPTY = "";
static const std::wstring WEMPTY = L"";

enum class LogLevel : int { ERR, WARNING, INFO, SUCCESS };

std::string to_string(LogLevel level) {
  switch (level) {
    case LogLevel::ERR: {
      return "ERROR";
    }
    case LogLevel::INFO: {
      return "INFO";
    }
    case LogLevel::WARNING: {
      return "WARNING";
    }
    case LogLevel::SUCCESS: {
      return "SUCCESS";
    }
  }
  return EMPTY;
}

typedef int AudioResult;

#ifdef _WIN32
static void CALLBACK waveOutProc(HWAVEOUT h_wave_out,
                                 UINT state,
                                 DWORD_PTR ptr,
                                 DWORD_PTR arg1,
                                 DWORD_PTR arg2);
#endif

#ifdef _WIN32
enum class Color {
  black = 0,
  blue = 1,
  green = 2,
  red = 4,
  yellow = 6,
  light_blue = 9,
  light_green = 0xA,
  light_red = 0xC,
  light_yellow = 0xE
};
#else
enum class Color {
  default_ = 39,
  black = 30,
  red = 31,
  green = 32,
  yellow = 33,
  blue = 34,
  light_red = 91,
  light_green = 92,
  light_yellow = 93,
  light_blue = 94
};
#endif

namespace Chrono {

std::string format(const time_t& rawtime, const char* format_str) {
  struct tm timeinfo;
  std::stringstream ss;
#ifdef _WIN32
  ::localtime_s(&timeinfo, &rawtime);
#elif __linux__
#ifdef __STDC_LIB_EXT1__
  ::localtime_s(&timeinfo, &rawtime);
#else
  ::localtime_r(&rawtime, &timeinfo);
#endif
#else
  timeinfo = *(::localtime(&rawtime));
#endif
  ss << std::put_time(&timeinfo, format_str);
  return ss.str();
}

typedef struct _current_time {
  time_t rawtime;
  long long millis;
  std::string to_string() { return format(rawtime, "%Y-%m-%d %H:%M:%S"); }
} CurrentTime;

CurrentTime current_time() {
  auto now = std::chrono::system_clock::now();
  auto rawtime = std::chrono::system_clock::to_time_t(now);
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count() %
                1000;
  return {rawtime, millis};
}

}  // namespace Chrono

namespace Bytes {
uint16_t bswap_uint16(uint16_t x) {
#ifdef _WIN32
  return _byteswap_ushort(x);
#else
  return __builtin_bswap16(x);
#endif
}
uint32_t bswap_uint32(uint32_t x) {
#ifdef _WIN32
  return _byteswap_ulong(x);
#else
  return __builtin_bswap32(x);
#endif
}
uint64_t bswap_uint64(uint64_t x) {
#ifdef _WIN32
  return _byteswap_uint64(x);
#else
  return __builtin_bswap64(x);
#endif
}
}  // namespace Bytes

namespace Strings {

namespace details {
template <typename T>
struct literal_traits {
  typedef char char_type;
  static const char* choose(const char* narrow, const wchar_t* wide) {
    (void*)wide;
    return narrow;
  }
  static char choose(const char narrow, const wchar_t wide) {
    (void)wide;
    return narrow;
  }
};

template <>
struct literal_traits<wchar_t> {
  typedef wchar_t char_type;
  static const wchar_t* choose(const char* narrow, const wchar_t* wide) {
    (void)narrow;
    return wide;
  }
  static wchar_t choose(const char narrow, const wchar_t wide) {
    (void)narrow;
    return wide;
  }
};

template <typename CharType>
int basic_vsnprintf(CharType* s,
                    size_t n,
                    const CharType* format,
                    va_list args) {
  return vsnprintf(s, n, format, args);
}

template <>
int basic_vsnprintf<wchar_t>(wchar_t* s,
                             size_t n,
                             const wchar_t* format,
                             va_list args) {
  return vswprintf(s, n, format, args);
}

template <typename CharType>
int basic_strcmp(const CharType* str1, const CharType* str2) {
  return strcmp(str1, str2);
}

template <>
int basic_strcmp<wchar_t>(const wchar_t* str1, const wchar_t* str2) {
  return wcscmp(str1, str2);
}

template <typename CharType>
int basic_strncmp(const CharType* str1, const CharType* str2, size_t num) {
  return strncmp(str1, str2, num);
}
template <>
int basic_strncmp<wchar_t>(const wchar_t* str1,
                           const wchar_t* str2,
                           size_t num) {
  return wcsncmp(str1, str2, num);
}

template <typename CharType>
size_t basic_strlen(const CharType* str) {
  return strlen(str);
}
template <>
size_t basic_strlen<wchar_t>(const wchar_t* str) {
  return wcslen(str);
}
template <typename CharType>
int basic_strcasecmp(const CharType* str1, const CharType* str2) {
#ifdef _WIN32
  return _stricmp(str1, str2);
#else
  return strcasecmp(str1, str2);
#endif
}
template <>
int basic_strcasecmp<wchar_t>(const wchar_t* str1, const wchar_t* str2) {
#ifdef _WIN32
  return _wcsicmp(str1, str2);
#else
  return wcscasecmp(str1, str2);
#endif
}
template <typename CharType>
int basic_strncasecmp(const CharType* str1, const CharType* str2, size_t n) {
#ifdef _WIN32
  return _strnicmp(str1, str2, n);
#else
  return strncasecmp(str1, str2, n);
#endif
}
template <>
int basic_strncasecmp<wchar_t>(const wchar_t* str1,
                               const wchar_t* str2,
                               size_t n) {
#ifdef _WIN32
  return _wcsnicmp(str1, str2, n);
#else
  return wcsncasecmp(str1, str2, n);
#endif
}

template <typename CharType>
bool starts_with(const CharType* str,
                 const CharType* prefix,
                 size_t str_size,
                 size_t prefix_size) {
  return (str_size != 0) && (prefix_size != 0) && (str_size >= prefix_size) &&
         basic_strncmp<CharType>(str, prefix, prefix_size) == 0;
}

template <typename CharType>
bool ends_with(const CharType* str,
               const CharType* suffix,
               size_t str_size,
               size_t suffix_size) {
  return (str_size != 0) && (suffix_size != 0) && (str_size >= suffix_size) &&
         basic_strncmp<CharType>(&str[str_size - suffix_size], suffix,
                                 suffix_size) == 0;
}

template <typename CharType>
bool starts_with_ignore_case(const CharType* str,
                             const CharType* prefix,
                             size_t str_size,
                             size_t prefix_size) {
  return (str_size != 0) && (prefix_size != 0) && (str_size >= prefix_size) &&
         basic_strncasecmp<CharType>(str, prefix, prefix_size) == 0;
}

template <typename CharType>
bool ends_with_ignore_case(const CharType* str,
                           const CharType* suffix,
                           size_t str_size,
                           size_t suffix_size) {
  return (str_size != 0) && (suffix_size != 0) && (str_size >= suffix_size) &&
         basic_strncasecmp<CharType>(&str[str_size - suffix_size], suffix,
                                     suffix_size) == 0;
}
template <typename CharType>
bool contains_ignore_case(const CharType* str1,
                          const CharType* str2,
                          size_t str1_len,
                          size_t str2_len) {
  auto it = std::search(str1, str1 + str1_len, str2, str2 + str2_len,
                        [](CharType ch1, CharType ch2) {
                          return std::toupper<CharType>(ch1, std::locale()) ==
                                 std::toupper<CharType>(ch2, std::locale());
                        });
  return (str1_len != 0) && (str2_len != 0) && (it != (str1 + str1_len));
}
template <typename CharType>
bool contains(const CharType* str1,
              const CharType* str2,
              size_t str1_len,
              size_t str2_len) {
  auto it = std::search(str1, str1 + str1_len, str2, str2 + str2_len);
  return (str1_len != 0) && (str2_len != 0) && (it != (str1 + str1_len));
}

}  // namespace details

#define LITERAL(T, x) details::literal_traits<T>::choose(x, L##x)

std::wstring to_wstring(const char* str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  return converter.from_bytes(str);
}

std::string to_string(const wchar_t* wstr) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

  return converter.to_bytes(wstr);
}
template <typename CharType>
std::basic_string<CharType> trim_left(const std::basic_string<CharType>& text) {
  std::basic_string<CharType> text_copy = text;
  text_copy.erase(
      std::begin(text_copy),
      std::find_if(std::begin(text_copy), std::end(text_copy),
                   [](CharType c) { return !std::isspace(c, std::locale()); }));
  return text_copy;
}
template <typename CharType>
std::basic_string<CharType> trim_right(
    const std::basic_string<CharType>& text) {
  std::basic_string<CharType> text_copy = text;
  text_copy.erase(
      std::find_if(std::rbegin(text_copy), std::rend(text_copy),
                   [](CharType c) { return !std::isspace(c, std::locale()); })
          .base(),
      std::end(text_copy));
  return text_copy;
}
template <typename CharType>
std::basic_string<CharType> trim(const std::basic_string<CharType>& text) {
  return trim_left(trim_right(text));
}
template <typename CharType>
std::vector<std::basic_string<CharType>> split(
    const std::basic_string<CharType>& text,
    CharType delim) {
  std::vector<std::basic_string<CharType>> tokens;
  if (delim == LITERAL(CharType, '\0')) {
    tokens.push_back(text);
    return tokens;
  }
  auto i = 0;
  auto start_pos = text.find(delim);
  while (start_pos != std::basic_string<CharType>::npos) {
    tokens.push_back(text.substr(i, start_pos - i));
    i = ++start_pos;
    start_pos = text.find(delim, start_pos);
  }
  if (start_pos == std::basic_string<CharType>::npos) {
    tokens.push_back(text.substr(i, text.length()));
  }
  return tokens;
}
template <typename CharType>
std::vector<std::basic_string<CharType>> split_chars(
    const std::basic_string<CharType>& text,
    const std::basic_string<CharType>& delims) {
  std::vector<std::basic_string<CharType>> tokens;
  if (delims.empty()) {
    tokens.push_back(text);
    return tokens;
  }
  size_t start_pos = text.find_first_not_of(delims), end_pos = 0;
  while ((end_pos = text.find_first_of(delims, start_pos)) !=
         std::basic_string<CharType>::npos) {
    tokens.push_back(text.substr(start_pos, end_pos - start_pos));
    start_pos = text.find_first_not_of(delims, end_pos);
  }
  if (start_pos != std::string::npos) {
    tokens.push_back(text.substr(start_pos));
  }

  return tokens;
}
template <typename CharType>
std::vector<std::basic_string<CharType>> split_strings(
    const std::basic_string<CharType>& text,
    const std::basic_string<CharType>& delim) {
  std::vector<std::basic_string<CharType>> tokens;
  if (delim.empty()) {
    tokens.push_back(text);
    return tokens;
  }
  size_t last_pos = 0;
  size_t next_pos = 0;
  while ((next_pos = text.find(delim, last_pos)) !=
         std::basic_string<CharType>::npos) {
    auto token = text.substr(last_pos, next_pos - last_pos);
    tokens.push_back(token);
    last_pos = next_pos + delim.length();
  }
  auto token = text.substr(last_pos);
  tokens.push_back(token);
  return tokens;
}
template <typename CharType>
std::basic_string<CharType> replace_all(std::basic_string<CharType>& text,
                                        const std::basic_string<CharType>& from,
                                        const std::basic_string<CharType>& to) {
  size_t start_pos = 0;
  while ((start_pos = text.find(from, start_pos)) !=
         std::basic_string<CharType>::npos) {
    text.replace(text.find(from), from.length(), to);
    start_pos += to.length();
  }
  return text;
}
template <typename CharType>
std::basic_string<CharType> join(
    std::vector<std::basic_string<CharType>>& tokens,
    std::basic_string<CharType> token) {
  std::basic_string<CharType> output;
  for (auto it = tokens.begin(); it != tokens.end(); ++it) {
    if (it != tokens.begin()) {
      output.append(token);
    }
    output.append(*it);
  }
  return output;
}

template <typename CharType>
bool starts_with(const std::basic_string<CharType>& str,
                 const std::basic_string<CharType>& prefix) {
  return details::starts_with<CharType>(str.c_str(), prefix.c_str(), str.size(),
                                        prefix.size());
}

template <typename CharType>
bool starts_with(const std::basic_string<CharType>& str,
                 const CharType* prefix) {
  return (prefix != nullptr) && (details::starts_with<CharType>(
                                    str.c_str(), prefix, str.size(),
                                    details::basic_strlen<CharType>(prefix)));
}
template <typename CharType>
bool starts_with(const CharType* str,
                 const std::basic_string<CharType>& prefix) {
  return (str != nullptr) &&
         (details::starts_with<CharType>(str, prefix.c_str(),
                                         details::basic_strlen<CharType>(str),
                                         prefix.size()));
}

template <typename CharType>
bool starts_with(const CharType* str, const CharType* prefix) {
  return (str != nullptr) && (prefix != nullptr) &&
         (details::starts_with<CharType>(
             str, prefix, details::basic_strlen<CharType>(str),
             details::basic_strlen<CharType>(prefix)));
}

template <typename CharType>
bool starts_with_ignore_case(const std::basic_string<CharType>& str,
                             const std::basic_string<CharType>& prefix) {
  return details::starts_with_ignore_case<CharType>(str.c_str(), prefix.c_str(),
                                                    str.size(), prefix.size());
}

template <typename CharType>
bool starts_with_ignore_case(const std::basic_string<CharType>& str,
                             const CharType* prefix) {
  return (prefix != nullptr) && (details::starts_with_ignore_case<CharType>(
                                    str.c_str(), prefix, str.size(),
                                    details::basic_strlen<CharType>(prefix)));
}
template <typename CharType>
bool starts_with_ignore_case(const CharType* str,
                             const std::basic_string<CharType>& prefix) {
  return (str != nullptr) &&
         (details::starts_with_ignore_case<CharType>(
             str, prefix.c_str(), details::basic_strlen<CharType>(str),
             prefix.size()));
}

template <typename CharType>
bool starts_with_ignore_case(const CharType* str, const CharType* prefix) {
  return (str != nullptr) && (prefix != nullptr) &&
         (details::starts_with_ignore_case<CharType>(
             str, prefix, details::basic_strlen<CharType>(str),
             details::basic_strlen<CharType>(prefix)));
}

template <typename CharType>
bool ends_with(const std::basic_string<CharType>& str,
               const std::basic_string<CharType>& prefix) {
  return details::ends_with<CharType>(str.c_str(), prefix.c_str(), str.size(),
                                      prefix.size());
}

template <typename CharType>
bool ends_with(const std::basic_string<CharType>& str, const CharType* prefix) {
  return (prefix != nullptr) && (details::ends_with<CharType>(
                                    str.c_str(), prefix, str.size(),
                                    details::basic_strlen<CharType>(prefix)));
}
template <typename CharType>
bool ends_with(const CharType* str, const std::basic_string<CharType>& prefix) {
  return (str != nullptr) &&
         (details::ends_with<CharType>(str, prefix.c_str(),
                                       details::basic_strlen<CharType>(str),
                                       prefix.size()));
}

template <typename CharType>
bool ends_with(const CharType* str, const CharType* prefix) {
  return (str != nullptr) && (prefix != nullptr) &&
         (details::ends_with<CharType>(
             str, prefix, details::basic_strlen<CharType>(str),
             details::basic_strlen<CharType>(prefix)));
}

template <typename CharType>
bool ends_with_ignore_case(const std::basic_string<CharType>& str,
                           const std::basic_string<CharType>& prefix) {
  return details::ends_with_ignore_case<CharType>(str.c_str(), prefix.c_str(),
                                                  str.size(), prefix.size());
}

template <typename CharType>
bool ends_with_ignore_case(const std::basic_string<CharType>& str,
                           const CharType* prefix) {
  return (prefix != nullptr) && (details::ends_with_ignore_case<CharType>(
                                    str.c_str(), prefix, str.size(),
                                    details::basic_strlen<CharType>(prefix)));
}
template <typename CharType>
bool ends_with_ignore_case(const CharType* str,
                           const std::basic_string<CharType>& prefix) {
  return (str != nullptr) &&
         (details::ends_with_ignore_case<CharType>(
             str, prefix.c_str(), details::basic_strlen<CharType>(str),
             prefix.size()));
}

template <typename CharType>
bool ends_with_ignore_case(const CharType* str, const CharType* prefix) {
  return (str != nullptr) && (prefix != nullptr) &&
         (details::ends_with_ignore_case<CharType>(
             str, prefix, details::basic_strlen<CharType>(str),
             details::basic_strlen<CharType>(prefix)));
}

template <typename CharType>
bool equals_ignore_case(const std::basic_string<CharType>& str1,
                        const std::basic_string<CharType>& str2) {
  return (str1.size() == str2.size()) &&
         (details::basic_strcasecmp<CharType>(str1.c_str(), str2.c_str())) == 0;
}
template <typename CharType>
bool equals_ignore_case(const std::basic_string<CharType>& str1,
                        const CharType* str2) {
  return (str2 != nullptr) &&
         (details::basic_strcasecmp<CharType>(str1.c_str(), str2)) == 0;
}
template <typename CharType>
bool equals_ignore_case(const CharType* str1,
                        const std::basic_string<CharType>& str2) {
  return (str1 != nullptr) &&
         (details::basic_strcasecmp<CharType>(str1, str2.c_str())) == 0;
}
template <typename CharType>
bool equals_ignore_case(const CharType* str1, const CharType* str2) {
  return (str1 != nullptr) && (str2 != nullptr) &&
         (details::basic_strcasecmp<CharType>(str1, str2)) == 0;
}

template <typename CharType>
bool contains_ignore_case(const std::basic_string<CharType>& str1,
                          const std::basic_string<CharType>& str2) {
  return details::contains_ignore_case<CharType>(str1.c_str(), str2.c_str(),
                                                 str1.size(), str2.size());
}
template <typename CharType>
bool contains_ignore_case(const CharType* str1,
                          const std::basic_string<CharType>& str2) {
  return (str1 != nullptr) &&
         details::contains_ignore_case<CharType>(
             str1, str2.c_str(), details::basic_strlen<CharType>(str1),
             str2.size());
}

template <typename CharType>
bool contains_ignore_case(const std::basic_string<CharType>& str1,
                          const CharType* str2) {
  return (str2 != nullptr) && details::contains_ignore_case<CharType>(
                                  str1.c_str(), str2, str1.size(),
                                  details::basic_strlen<CharType>(str2));
}

template <typename CharType>
bool contains_ignore_case(const CharType* str1, const CharType* str2) {
  return (str1 != nullptr) && (str2 != nullptr) &&
         details::contains_ignore_case<CharType>(
             str1, str2, details::basic_strlen<CharType>(str1),
             details::basic_strlen<CharType>(str2));
}
template <typename CharType>
bool contains(const std::basic_string<CharType>& str1,
              const std::basic_string<CharType>& str2) {
  return details::contains<CharType>(str1.c_str(), str2.c_str(), str1.size(),
                                     str2.size());
}
template <typename CharType>
bool contains(const CharType* str1, const std::basic_string<CharType>& str2) {
  return (str1 != nullptr) &&
         details::contains<CharType>(str1, str2.c_str(),
                                     details::basic_strlen<CharType>(str1),
                                     str2.size());
}

template <typename CharType>
bool contains(const std::basic_string<CharType>& str1, const CharType* str2) {
  return (str2 != nullptr) &&
         details::contains<CharType>(str1.c_str(), str2, str1.size(),
                                     details::basic_strlen<CharType>(str2));
}

template <typename CharType>
bool contains(const CharType* str1, const CharType* str2) {
  return (str1 != nullptr) && (str2 != nullptr) &&
         details::contains<CharType>(str1, str2,
                                     details::basic_strlen<CharType>(str1),
                                     details::basic_strlen<CharType>(str2));
}
template <typename CharType>
std::basic_string<CharType> format(const CharType* fmt, ...) {
  CharType buffer[MAXBUFFERSIZE];
  va_list args;
  va_start(args, fmt);
  int sz = details::basic_vsnprintf<CharType>(buffer, MAXBUFFERSIZE, fmt, args);
  va_end(args);
  int copy_len = (std::min)(sz, MAXBUFFERSIZE);
  std::basic_string<CharType> output(buffer, copy_len);
  return output;
}

template <typename CharType>
std::basic_string<CharType> to_upper_case(
    const std::basic_string<CharType>& input) {
  std::basic_string<CharType> copy_input(input);
  std::transform(copy_input.begin(), copy_input.end(), copy_input.begin(),
                 [](CharType& c) { return std::toupper(c, std::locale()); });
  return copy_input;
}

template <typename CharType>
std::basic_string<CharType> to_lower_case(
    const std::basic_string<CharType>& input) {
  std::basic_string<CharType> copy_input(input);
  std::transform(copy_input.begin(), copy_input.end(), copy_input.begin(),
                 [](CharType& c) { return std::tolower(c, std::locale()); });
  return copy_input;
}

}  // namespace Strings

namespace System {
namespace Options {

typedef struct _Option {
  std::string short_value;
  std::string long_value;
  std::string description;
  std::string extras;
  _Option(std::initializer_list<const char*> elements) {
    auto it = elements.begin();
    _Option::short_value = *it;
    _Option::long_value = *(it + 1);
    _Option::description = *(it + 2);
    _Option::extras = *(it + 3);
  }
  std::string to_string() {
    std::string str;
    str.append("\t");
    str.append(_Option::short_value);
    str.append(", ");
    str.append(_Option::long_value);
    str.append(_Option::extras);
    str.append(_Option::description);
    str.append("\n");
    return str;
  }
  bool is_equal(const std::string& flag) {
    return (Strings::equals_ignore_case(flag, _Option::short_value) ||
            Strings::equals_ignore_case(flag, _Option::long_value));
  }
} Option;

Option HELP = {"-h", "--help", "Show this help message", "\t\t"};
Option DIRECTORY = {"-d", "--directory", "Specify the directory path",
                    " <dir>\t"};
Option FILES = {"-f", "--files", "Specify the list of files", " <file(s)>\t"};
Option RECURSIVE = {"-r", "--recursive", "Specify the directory path",
                    " <dir>\t"};
Option VERSION = {"-v", "--version", "Show version", "\t\t"};

std::vector<Option> list_of_options() {
  std::vector<Option> options;
  options.push_back(HELP);
  options.push_back(FILES);
  options.push_back(DIRECTORY);
  options.push_back(RECURSIVE);
  options.push_back(VERSION);
  return options;
}

bool is_help_option(const std::string& flag) {
  return HELP.is_equal(flag);
}

bool is_directory_option(const std::string& flag) {
  return DIRECTORY.is_equal(flag);
}

bool is_files_option(const std::string& flag) {
  return FILES.is_equal(flag);
}

bool is_recursive_option(const std::string& flag) {
  return RECURSIVE.is_equal(flag);
}
bool is_version_option(const std::string& flag) {
  return VERSION.is_equal(flag);
}
}  // namespace Options

namespace Process {
void exit_process(int exit_code) {
#ifdef __linux__
  ::_Exit(exit_code);
#elif _WIN32
  ::ExitProcess(exit_code);
#endif
}
}  // namespace Process

namespace Console {
void print_color(std::string message, const Color color = Color::light_green) {
#ifdef _WIN32
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO consoleScreenBufferInfo;
  GetConsoleScreenBufferInfo(hConsole, &consoleScreenBufferInfo);
  auto original_color = consoleScreenBufferInfo.wAttributes;
  SetConsoleTextAttribute(hConsole,
                          static_cast<WORD>(color) | (original_color & 0xF0));
  std::cout << message;
  SetConsoleTextAttribute(hConsole, original_color);
#elif __linux__
  std::cout << "\033[" << static_cast<int>(color) << "m" << message;
  std::cout << "\033[" << static_cast<int>(Color::default_) << "m";
#endif
}

void print_error(std::string message) {
  print_color(message, Color::light_red);
}

static void show_usage() {
  std::string usage;
#ifdef _WIN32
  usage.append("Usage:  looper.exe [option] <inputs>\nOptions:\n");
#else
  usage.append("Usage:  looper [option] <inputs>\nOptions:\n");
#endif

  for (auto& flag : System::Options::list_of_options()) {
    std::string str = flag.to_string();
    usage.append(str);
  }
  std::cout << usage << std::endl;
}
static void show_version() {
  std::string version_info = "Looper\nVersion: ";
  version_info.append(LOOPER_VERSION);
  version_info.append("\n");
  version_info.append("Git Branch: ");
  version_info.append(GIT_BRANCH);
  version_info.append("\n");
  version_info.append("Git Commit: ");
  version_info.append(GIT_COMMIT_HASH);
  version_info.append("\n");
  std::cout << version_info;
}

std::string console_progress(const std::string& label,
                             uint64_t seconds,
                             uint64_t total_seconds,
                             int console_width) {
  int progress_bar_width = console_width - static_cast<int>(label.size());
  uint64_t position = (seconds * progress_bar_width) / total_seconds;

#ifdef _WIN32
  std::string progress = "";
#else
  std::string progress = "\x1B[0E";
#endif

  progress += label;
  progress += "[";

  for (int i = 0; i < progress_bar_width; i++) {
    if (i < static_cast<int64_t>(position))
      progress += "=";
    else
      progress += " ";
  }
  progress += "] ";

  uint64_t minutes = seconds / 60;
  uint64_t current_seconds = seconds % 60;
  progress +=
      Strings::format("%.2" PRIu64 ":%.2" PRIu64, minutes, current_seconds);
  progress += "s\r";
  return progress;
}

int get_console_width() {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO consoleScreenBufferInfo;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),
                             &consoleScreenBufferInfo);
  return consoleScreenBufferInfo.srWindow.Right -
         consoleScreenBufferInfo.srWindow.Left;
#else
  struct winsize screen_size;
  ioctl(0, TIOCGWINSZ, &screen_size);
  return screen_size.ws_col;
#endif
}

void show_progress(uint64_t seconds, uint64_t total_seconds, int width) {
  std::string progress =
      console_progress("Play: ", seconds, total_seconds, width);
  std::flush(std::cout);
  print_color(progress);
  std::flush(std::cout);
}

#ifdef _WIN32
std::vector<std::string> get_commandline_flags()
#else
std::vector<std::string> get_commandline_flags(int argc, char* argv[])
#endif
{
  std::vector<std::string> flags;

#ifdef _WIN32
  LPWSTR* szArgList;
  int nArgs;

  szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
  if (nullptr == szArgList || nArgs < 2) {
    show_usage();
    Process::exit_process(EXIT_FAILURE);
  }
  for (int i = 0; i < nArgs; ++i) {
    flags.push_back(Strings::to_string(szArgList[i]));
  }

  LocalFree(szArgList);
#else
  if (nullptr == argv || argc < 2) {
    show_usage();
    Process::exit_process(EXIT_FAILURE);
  }
  for (int i = 0; i < argc; ++i) {
    flags.push_back(argv[i]);
  }

#endif
  return flags;
}
}  // namespace Console

namespace Files {
class Extensions {
 public:
  static const std::string OPUS;
  static const std::string MP3;
  static const std::string OGG;
  static const std::string FLAC;
  static const std::string WAV;
  static const std::string OGA;
  static const std::string AIF;
  static const std::string AIFF;
  static const std::string CAF;
  static const std::string CAFF;
  static const std::string M4A;
  static const std::string ALAC;
  static const std::string AAC;
};

const std::string Extensions::OPUS = ".opus";
const std::string Extensions::MP3 = ".mp3";
const std::string Extensions::OGG = ".ogg";
const std::string Extensions::FLAC = ".flac";
const std::string Extensions::WAV = ".wav";
const std::string Extensions::OGA = ".opus";
const std::string Extensions::AIF = ".aif";
const std::string Extensions::AIFF = ".aiff";
const std::string Extensions::CAF = ".caf";
const std::string Extensions::CAFF = ".caff";
const std::string Extensions::ALAC = ".alac";
const std::string Extensions::M4A = ".m4a";
const std::string Extensions::AAC = ".aac";

static std::vector<std::string> default_extensions = {
    Extensions::OPUS, Extensions::MP3,  Extensions::OGG,  Extensions::FLAC,
    Extensions::WAV,  Extensions::OGA,  Extensions::AIFF, Extensions::AIF,
    Extensions::CAF,  Extensions::CAFF, Extensions::ALAC, Extensions::M4A,
    Extensions::AAC};

std::vector<fs::path> list_files(
    fs::path& directory,
    std::function<bool(const fs::path&)> path_filter) {
  std::vector<fs::path> found_files;
  if (fs::exists(directory) && fs::is_directory(directory)) {
    for (const auto& entry : fs::directory_iterator(directory)) {
      if (path_filter(entry)) {
        found_files.push_back(entry.path());
      }
    }
  }
  return found_files;
}
std::vector<fs::path> list_files_recursive(
    fs::path& directory,
    std::function<bool(const fs::path&)> path_filter) {
  std::vector<fs::path> found_files;
  if (fs::exists(directory) && fs::is_directory(directory)) {
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
      if (path_filter(entry)) {
        found_files.push_back(entry.path());
      }
    }
  }
  return found_files;
}
bool is_required_file(const fs::path& entry) {
  std::sort(default_extensions.begin(), default_extensions.end());
  std::string extension = Strings::to_lower_case(entry.extension().string());
  return fs::is_regular_file(entry) &&
         std::binary_search(default_extensions.begin(),
                            default_extensions.end(), extension);
}

}  // namespace Files
}  // namespace System

// namespace App
namespace Logging {

class TraceMessage {
 public:
  static const std::string TRACE_FORMAT;
  static void log(const char* function_name,
                  const char* log_,
                  const char* filename,
                  const int linenumber,
                  LogLevel level) {
    Chrono::CurrentTime current_time = Chrono::current_time();
    std::string date_str = current_time.to_string();
    std::string log_info = Strings::format(
        TRACE_FORMAT.c_str(), date_str.c_str(), current_time.millis,
        to_string(level).c_str(), function_name, log_, filename, linenumber);
    switch (level) {
      case LogLevel::ERR: {
        System::Console::print_error(log_info);
      } break;
      case LogLevel::INFO: {
        std::cout << log_info;
      } break;
      case LogLevel::WARNING: {
        System::Console::print_color(log_info, Color::yellow);
      } break;
      case LogLevel::SUCCESS: {
        System::Console::print_color(log_info);
      } break;
    }
  }
};
const std::string TraceMessage::TRACE_FORMAT =
    "%s.%03lld %s: %s (%s) [%s:%d]\n";
}  // namespace Logging

enum class AudioStatus : int {
  kSuccess = 0,
  kIoError = 1,
  kAudioDeviceError = 2,
  kUknownError = 3
};

typedef struct _AudioFormat {
  int channels, encoding, sample_rate, bits_per_sample, reserved;
  uint64_t total_samples, duration, avg_bytes_per_second;
  bool is_little_endian;
  _AudioFormat() {
    int num = 1;
    is_little_endian = (*(char*)&num == 1);
  }

} AudioFormat;

#ifdef _WIN32

// based on
// https://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=4422&lngWId=3
// based on
// https://chromium.googlesource.com/chromium/src.git/+/master/media/audio/win/waveout_output_win.cc
static const int kMaxChannelsToMask = 8;
static const unsigned int kChannelsToMask[kMaxChannelsToMask + 1] = {
    0,
    // 1 = Mono
    SPEAKER_FRONT_CENTER,
    // 2 = Stereo
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT,
    // 3 = Stereo + Center
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER,
    // 4 = Quad
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT,
    // 5 = 5.0
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
        SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
    // 6 = 5.1
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
    // 7 = 6.1
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
        SPEAKER_BACK_CENTER,
    // 8 = 7.1
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
        SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT};

std::string mmresult_to_string(MMRESULT mmrError) {
  std::wstring result(MAXERRORLENGTH + 1, '\0');
  waveOutGetErrorTextW(mmrError, &result[0], MAXERRORLENGTH);
  return Strings::to_string(&result[0]);
}
class SimplePlayer {
 public:
  explicit SimplePlayer(int block_count = default_block_count,
                        int block_size = default_block_size)
      : format{} {
    SimplePlayer::free_blocks_count = block_count;
    SimplePlayer::block_count = block_count;
    SimplePlayer::block_size = block_size;

    TRACE_INFO(Strings::format("Initializing player with free_blocks_count(%d)",
                               free_blocks_count)
                   .c_str());
    InitializeCriticalSection(&waveCriticalSection);
  }

  void play(const fs::path& path){};
  void setupblocks() {
    current_block = 0;
    blocks = std::make_unique<unsigned char[]>(block_count * get_block_size());
    for (int ix = 0; ix < block_count; ++ix) {
      WAVEHDR* block = get_block(ix);
      block->lpData = reinterpret_cast<char*>(block) + sizeof(WAVEHDR);
      block->dwBufferLength = block_size;
      block->dwBytesRecorded = 0;
      block->dwFlags = WHDR_DONE;
      block->dwLoops = 0;
    }
  }

  void set_format() {
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = static_cast<WORD>(format.channels);
    wfx.Format.nSamplesPerSec = static_cast<WORD>(format.sample_rate);
    wfx.Format.wBitsPerSample = static_cast<WORD>(format.bits_per_sample);
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx.Format.nBlockAlign =
        (wfx.Format.nChannels * wfx.Format.wBitsPerSample) / 8;
    wfx.Format.nAvgBytesPerSec =
        wfx.Format.nBlockAlign * wfx.Format.nSamplesPerSec;
    if (format.channels > kMaxChannelsToMask) {
      wfx.dwChannelMask = kChannelsToMask[kMaxChannelsToMask];
    } else {
      wfx.dwChannelMask = kChannelsToMask[format.channels];
    }

    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    wfx.Samples.wValidBitsPerSample = static_cast<WORD>(format.bits_per_sample);
  }

  void open() {
    MMRESULT result;
    if ((result = ::waveOutOpen(
             &hWaveOut, WAVE_MAPPER, reinterpret_cast<LPCWAVEFORMATEX>(&wfx),
             reinterpret_cast<DWORD_PTR>(waveOutProc),
             reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION)) !=
        MMSYSERR_NOERROR) {
      TRACE_INFO(mmresult_to_string(result).c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    } else {
      TRACE_INFO("Opening Device");
    }
  }

  int bits_per_sample() { return format.bits_per_sample; }
  uint64_t total_duration() { return format.duration; }
  int channels() { return format.channels; }
  int sample_rate() { return format.sample_rate; }

  void increment_block() {
    EnterCriticalSection(&waveCriticalSection);
    this->free_blocks_count++;
    LeaveCriticalSection(&waveCriticalSection);
  }

  void decrement_block() {
    EnterCriticalSection(&waveCriticalSection);
    this->free_blocks_count--;
    LeaveCriticalSection(&waveCriticalSection);
  }

  void freeblocks() {
    while (free_blocks_count < block_count)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int ix = 0; ix < block_count; ++ix) {
      ::waveOutUnprepareHeader(hWaveOut, get_block(ix), sizeof(WAVEHDR));
    }
    blocks.reset();
  }

  void writeaudio(LPSTR data, int size) {
    WAVEHDR* current;
    int remain;

    current = get_block(current_block);

    while (size > 0) {
      if (current->dwFlags & WHDR_PREPARED)
        waveOutUnprepareHeader(hWaveOut, current, sizeof(WAVEHDR));

      if (size < static_cast<int>(block_size - current->dwUser)) {
        memcpy(current->lpData + current->dwUser, data, size);
        current->dwUser += size;
        break;
      }

      remain = static_cast<int>(block_size - current->dwUser);
      memcpy(current->lpData + current->dwUser, data, remain);
      size -= remain;
      data += remain;
      current->dwBufferLength = block_size;

      waveOutPrepareHeader(hWaveOut, current, sizeof(WAVEHDR));
      waveOutWrite(hWaveOut, current, sizeof(WAVEHDR));

      decrement_block();

      while (!free_blocks_count)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

      current_block++;
      current_block %= block_count;

      current = get_block(current_block);
      current->dwUser = 0;
    }
  }

  ~SimplePlayer() { DeleteCriticalSection(&waveCriticalSection); }
  void close() {
    ::waveOutClose(hWaveOut);
    TRACE_INFO("closing device");
  }

  enum {
    default_buffer_size = 0x400,
    default_block_count = 0x20,
    default_block_size = 0x2000
  };

 protected:
  AudioFormat format;

 private:
  int get_block_size() { return (sizeof(WAVEHDR) + block_size + 15u) & (~15u); }

  WAVEHDR* get_block(int position) {
    return reinterpret_cast<WAVEHDR*>(&blocks[get_block_size() * position]);
  }
  std::unique_ptr<unsigned char[]> blocks;
  WAVEFORMATEXTENSIBLE wfx;
  // WAVEFORMATEX    wfx;
  HWAVEOUT hWaveOut;
  CRITICAL_SECTION waveCriticalSection;
  volatile int free_blocks_count;
  int block_size, block_count, current_block;
};

#elif __linux__
class SimplePlayer {
 public:
  SimplePlayer() : format() {}
  ~SimplePlayer() {}
  snd_pcm_format_t get_pcm_format() {
    switch (format.bits_per_sample) {
      case 64:
        return (format.is_little_endian) ? SND_PCM_FORMAT_FLOAT64_LE
                                         : SND_PCM_FORMAT_FLOAT64_BE;
      case 32:
        return (format.is_little_endian) ? SND_PCM_FORMAT_S32_LE
                                         : SND_PCM_FORMAT_S32_BE;
      case 24:
        return (format.is_little_endian) ? SND_PCM_FORMAT_S24_LE
                                         : SND_PCM_FORMAT_S24_BE;
      case 16:
        return (format.is_little_endian) ? SND_PCM_FORMAT_S16_LE
                                         : SND_PCM_FORMAT_S16_BE;
      case 8:
        return SND_PCM_FORMAT_S8;
      default:
        return SND_PCM_FORMAT_UNKNOWN;
    }
  }
  int bits_per_sample() { return format.bits_per_sample; }
  int channels() { return format.channels; }
  int sample_rate() { return format.sample_rate; }
  uint64_t total_duration() { return format.duration; }
  virtual void play(const fs::path& path) = 0;
  void open() {
    AudioResult result;
    if ((result = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK,
                               0)) < 0) {
      TRACE_ERROR(Strings::format("Can't open \"%s\" PCM device. %s",
                                  PCM_DEVICE, snd_strerror(result))
                      .c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    }

    snd_pcm_hw_params_alloca(&params);

    snd_pcm_hw_params_any(pcm_handle, params);

    if ((result = snd_pcm_hw_params_set_access(
             pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
      TRACE_ERROR(Strings::format("Can't set interleaved mode. %s",
                                  snd_strerror(result))
                      .c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    }

    if ((result = snd_pcm_hw_params_set_format(pcm_handle, params,
                                               get_pcm_format())) < 0) {
      TRACE_ERROR(Strings::format("Can't set format. %s", snd_strerror(result))
                      .c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    }
    if ((result = snd_pcm_hw_params_set_channels(pcm_handle, params,
                                                 format.channels)) < 0) {
      TRACE_ERROR(
          Strings::format("Can't set channels number. %s", snd_strerror(result))
              .c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    }

    if (((result = snd_pcm_hw_params_set_rate_near(
              pcm_handle, params,
              reinterpret_cast<unsigned int*>(&format.sample_rate), 0))) < 0) {
      TRACE_ERROR(
          Strings::format("Can't set sample_rate. %s", snd_strerror(result))
              .c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    }

    if ((result = snd_pcm_hw_params(pcm_handle, params)) < 0) {
      TRACE_ERROR(Strings::format("Can't set harware parameters. %s",
                                  snd_strerror(result))
                      .c_str());
      System::Process::exit_process(
          static_cast<int>(AudioStatus::kAudioDeviceError));
    }
  }
  void close() {
    if (pcm_handle) {
      snd_pcm_drain(pcm_handle);
      snd_pcm_close(pcm_handle);
    }
  }
  snd_pcm_uframes_t bytes_to_frames(ssize_t _bytes) {
    return snd_pcm_bytes_to_frames(pcm_handle, _bytes);
  }
  void writeaudio(const void* buffer, snd_pcm_uframes_t _frames) {
    AudioResult result;
    if ((result = snd_pcm_writei(pcm_handle, buffer, _frames)) == -EPIPE) {
      snd_pcm_prepare(pcm_handle);
    } else if (result < 0) {
      TRACE_ERROR(
          Strings::format("Can't write to PCM device. %s", snd_strerror(result))
              .c_str());
    }
  }

  snd_pcm_t* pcm_handle;
  snd_pcm_hw_params_t* params;
  snd_pcm_uframes_t frames;
  enum { default_buffer_size = 0x400 };
  AudioFormat format;
};
#endif

#ifdef _WIN32
static void CALLBACK waveOutProc(HWAVEOUT h_wave_out,
                                 UINT state,
                                 DWORD_PTR ptr,
                                 DWORD_PTR arg1,
                                 DWORD_PTR arg2) {
  (void)h_wave_out;
  (void)arg1;
  (void)arg2;

  if (state != WOM_DONE)
    return;

  SimplePlayer* player = reinterpret_cast<SimplePlayer*>(ptr);
  player->increment_block();
}
#endif

typedef struct _Metadata {
  std::string artist;
  std::string title;
  std::string year;
  std::string genre;
  std::string comment;
  std::string album;
} Metadata;

std::string to_string(const Metadata& meta) {
  return "Title: " + meta.title + "\nArtist: " + meta.artist +
         "\nAlbum: " + meta.album + "\nYear: " + meta.year +
         "\nComment: " + meta.comment + "\nGenre: " + meta.genre + "\n";
}

void print_info(const Metadata& meta) {
  System::Console::print_color("Playing Info\n", Color::light_yellow);
  System::Console::print_color(to_string(meta));
  System::Console::print_color("Starting to play\n", Color::light_yellow);
}

typedef struct _ChunkHeader {
  char chunk_id[4];
  uint32_t chunk_size;
} ChunkHeader;

typedef struct _WaveFormat {
  uint16_t format_tag;
  uint16_t channels;
  uint32_t sample_rate;
  uint32_t avg_bytes_per_second;
  uint16_t block_align;
  uint16_t bits_per_sample;
} WaveFormat;

size_t read_data(std::ifstream& f, void* buf, std::streamsize bufsize) {
  std::istream& is_ok = f.read(reinterpret_cast<char*>(buf), bufsize);
  return static_cast<size_t>(is_ok.gcount());
}

class WavPlayer : public SimplePlayer {
 public:
  size_t read_chunk(std::ifstream& f, ChunkHeader& hdr) {
    return read_data(f, &hdr, sizeof(hdr));
  }

  void set_format(const fs::path& path) {
    std::ifstream wave_file(path.native(), std::ifstream::binary);
    if (wave_file) {
      TRACE_INFO(Strings::format("Opened %s", path.c_str()).c_str());

      ChunkHeader header;
      int read_bytes = 0;

      read_bytes = read_chunk(wave_file, header);
      if (read_bytes < sizeof(ChunkHeader)) {
        TRACE_ERROR("Small header size");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
      if (Strings::details::basic_strncmp(header.chunk_id, "RIFF", 4) != 0) {
        TRACE_ERROR("Expected chunk 'RIFF' not detected");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }

      char wave_type[4];
      read_bytes = read_data(wave_file, wave_type, 4);
      if (read_bytes < 4) {
        TRACE_ERROR("Small header size");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
      if (Strings::details::basic_strncmp(wave_type, "WAVE", 4) != 0) {
        TRACE_ERROR("Expected chunk 'WAVE' not detected");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }

      read_bytes = read_chunk(wave_file, header);
      if (read_bytes < sizeof(ChunkHeader)) {
        TRACE_ERROR("Small header size");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
      WaveFormat wft;
      if (Strings::details::basic_strncmp(header.chunk_id, "fmt", 3) == 0) {
        if (header.chunk_size == sizeof(WaveFormat)) {
          read_bytes = read_data(wave_file, &wft, sizeof(wft));
          if (read_bytes < sizeof(wft)) {
            TRACE_ERROR("Small header size");
            System::Process::exit_process(
                static_cast<int>(AudioStatus::kIoError));
          }
        } else if (header.chunk_size >= sizeof(WaveFormat)) {
          read_bytes = read_data(wave_file, &wft, sizeof(wft));
          if (read_bytes < sizeof(wft)) {
            TRACE_ERROR("Small header size");
            System::Process::exit_process(
                static_cast<int>(AudioStatus::kIoError));
          }
          wave_file.ignore(header.chunk_size - sizeof(wft));
        } else {
          TRACE_ERROR("Invalid data size detected");
          System::Process::exit_process(0);
        }
      } else {
        TRACE_ERROR("Expected fmt");
        System::Process::exit_process(0);
      }
      while ((read_bytes = read_chunk(wave_file, header)) > 0) {
        if (read_bytes < sizeof(ChunkHeader)) {
          TRACE_ERROR("Small header size");
          System::Process::exit_process(
              static_cast<int>(AudioStatus::kIoError));
        }
        if (Strings::details::basic_strncmp(header.chunk_id, "data", 4) == 0) {
          format.bits_per_sample = wft.bits_per_sample;
          format.channels = wft.channels;
          format.sample_rate = wft.sample_rate;
          format.total_samples = header.chunk_size / wft.block_align;
          format.duration = format.total_samples / format.sample_rate;
          format.avg_bytes_per_second = wft.avg_bytes_per_second;

#ifdef _WIN32
          SimplePlayer::set_format();
#endif
          break;
        }
        wave_file.ignore(header.chunk_size);
      }
      if (read_bytes <= 0 || wave_file.eof()) {
        TRACE_ERROR("Wrong Format");
        System::Process::exit_process(0);
      }
    } else {
      TRACE_ERROR("Failed to open file");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }
  }
  void play(const fs::path& path) {
    set_format(path);
    std::ifstream wave_file(path.native(), std::ifstream::binary);
    if (wave_file) {
      int read_bytes = 0;
      Metadata mt;
      fs::path current_path(path);
      mt.title = current_path.stem().string();
      print_info(mt);

#ifdef _WIN32
      setupblocks();
#endif

      open();

      std::string buffer(default_buffer_size + 1, '\0');
      uint64_t total_read_bytes = 0, seconds = 0;
      int width =
          (std::max)(80 * System::Console::get_console_width() / 100, 50);

      for (;;) {
        std::istream& is_ok = wave_file.read(&buffer[0], default_buffer_size);
        read_bytes = static_cast<int>(is_ok.gcount());
        if (read_bytes <= 0)
          break;
#ifdef _WIN32
        writeaudio(&buffer[0], static_cast<int>(read_bytes));
#elif __linux__
        frames = bytes_to_frames(default_buffer_size);
        writeaudio(&buffer[0], frames);
#endif
        total_read_bytes += read_bytes;
        if (seconds == (total_read_bytes / format.avg_bytes_per_second)) {
          System::Console::show_progress(seconds, total_duration(), width);
          seconds++;
        }
      }
#ifdef _WIN32
      freeblocks();
#endif
      close();
      System::Console::print_color("Done Playing Song\n\n",
                                   Color::light_yellow);
    }
  }
};

typedef mpg123_handle MPG123Handle;

Metadata get_metadata(MPG123Handle* mh) {
  Metadata mt;

  auto copy_field = [](std::string& str, mpg123_string* input) {
    if (input != nullptr) {
      str = input->p;
    }
  };

  mpg123_scan(mh);
  AudioResult meta_result = mpg123_meta_check(mh);

  if (meta_result & MPG123_ID3) {
    mpg123_id3v1* v1;
    mpg123_id3v2* v2;
    AudioResult id3_result = mpg123_id3(mh, &v1, &v2);
    if (id3_result == MPG123_OK) {
      if (v1 != nullptr) {
        mt.title = v1->title;
        mt.artist = v1->artist;
        mt.album = v1->album;
        mt.year = v1->year;
        mt.comment = v1->comment;
        mt.genre = v1->genre;
      } else if (v2 != nullptr) {
        copy_field(mt.title, v2->title);
        copy_field(mt.artist, v2->artist);
        copy_field(mt.album, v2->album);
        copy_field(mt.year, v2->year);
        copy_field(mt.comment, v2->comment);
        copy_field(mt.genre, v2->genre);
      }
    }
  }
  return mt;
}

class MP3Player : public SimplePlayer {
 public:
  void set_format(MPG123Handle* mh) {
    mpg123_getformat(mh, reinterpret_cast<long int*>(&format.sample_rate),
                     &format.channels, &format.encoding);

    if (format.encoding & MPG123_ENC_FLOAT_64) {
      format.bits_per_sample = 64;
    } else if (format.encoding & MPG123_ENC_FLOAT_32) {
      format.bits_per_sample = 32;
    } else if (format.encoding & MPG123_ENC_16) {
      format.bits_per_sample = 16;
    } else {
      format.bits_per_sample = 8;
    }
    format.total_samples = mpg123_length(mh);
    format.duration = format.total_samples / format.sample_rate;
    format.avg_bytes_per_second =
        (format.channels * format.bits_per_sample * format.sample_rate) / 8;
#ifdef _WIN32
    SimplePlayer::set_format();
#endif
  }
  void play(const fs::path& path) {
    TRACE_INFO("I am starting to play");
    MPG123Handle* mh;

    size_t buffer_size, read_bytes;

    AudioResult result = MPG123_OK;

    result = mpg123_init();

    if (result != MPG123_OK) {
      TRACE_ERROR(
          Strings::format("mpg123_init error: %s", ErrorCodeToString(result))
              .c_str());
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }

    mh = mpg123_new(nullptr, &result);

    if (result != MPG123_OK) {
      TRACE_ERROR(
          Strings::format("mpg123_new error: %s", ErrorCodeToString(result))
              .c_str());
      cleanup(mh);
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }
#ifdef _WIN32
    std::string native_c_str = Strings::to_string(path.native().c_str());
    result = mpg123_open(mh, native_c_str.c_str());
#else
    result = mpg123_open(mh, path.native().c_str());
#endif

    if (result != MPG123_OK) {
      TRACE_ERROR(
          Strings::format("Cannot open file: %s", HandleErrorToString(mh))
              .c_str());
      cleanup(mh);
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    } else {
      TRACE_INFO(Strings::format("Opened %s", path.c_str()).c_str());
    }

    print_info(get_metadata(mh));
    set_format(mh);

    buffer_size = mpg123_outblock(mh);
    std::string buffer(buffer_size, '\0');

#ifdef _WIN32
    setupblocks();
#endif

    open();

    TRACE_INFO(Strings::format("Buffer size is %s\n", buffer_size).c_str());

    uint64_t total_read_bytes = 0, seconds = 0;
    int width = (std::max)(80 * System::Console::get_console_width() / 100, 50);

    while (mpg123_read(mh, reinterpret_cast<unsigned char*>(&buffer[0]),
                       buffer_size, &read_bytes) == MPG123_OK) {
      if (read_bytes <= 0)
        break;
#ifdef _WIN32
      writeaudio(&buffer[0], static_cast<int>(read_bytes));
#elif __linux__
      frames = bytes_to_frames(read_bytes);
      writeaudio(&buffer[0], frames);
#endif
      total_read_bytes += read_bytes;
      if (seconds == (total_read_bytes / format.avg_bytes_per_second)) {
        System::Console::show_progress(seconds, total_duration(), width);
        seconds++;
      }
    }

#ifdef _WIN32
    freeblocks();
#endif

    cleanup(mh);
    close();
    System::Console::print_color("Done Playing Song\n\n", Color::light_yellow);
  }

  const char* ErrorCodeToString(int error_code) {
    return mpg123_plain_strerror(error_code);
  }
  const char* HandleErrorToString(MPG123Handle* mh) {
    return mpg123_strerror(mh);
  }

  void cleanup(MPG123Handle* mh) {
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
  }
};

typedef vorbis_info VorbisInfo;
typedef vorbis_comment VorbisComment;

void add_meta(Metadata* meta, std::string& key, std::string& value) {
  if (Strings::contains_ignore_case(key.c_str(), "artist")) {
    if (meta->artist.empty()) {
      meta->artist.append(value);
    } else {
      meta->artist.append("; ");
      meta->artist.append(value);
    }
  } else if (Strings::contains_ignore_case(key.c_str(), "title")) {
    meta->title.append(value);
  } else if (Strings::contains_ignore_case(key.c_str(), "year")) {
    meta->year.append(value);
  } else if (Strings::contains_ignore_case(key.c_str(), "date")) {
    meta->year.append(value);
  } else if (Strings::contains_ignore_case(key.c_str(), "genre")) {
    if (meta->genre.empty()) {
      meta->genre.append(value);
    } else {
      meta->genre.append("; ");
      meta->genre.append(value);
    }
  } else if (Strings::contains_ignore_case(key.c_str(), "album")) {
    meta->album.append(value);
  } else if (Strings::contains_ignore_case(key.c_str(), "comment")) {
    meta->comment.append(value);
  }
};

Metadata get_metadata(OggVorbis_File* vf) {
  Metadata mt;
  const VorbisComment* comment = ov_comment(vf, -1);
  if (comment != nullptr) {
    for (int i = 0; i < comment->comments; i++) {
      size_t comment_length = comment->comment_lengths[i];
      std::string comment_str(comment_length + 1, '\0');
      strncpy(&comment_str[0], comment->user_comments[i], comment_length);
      std::vector<std::string> tokens = Strings::split(comment_str, '=');
      if (tokens.size() > 1) {
        for (size_t j = 1; j < tokens.size(); j++) {
          add_meta(&mt, tokens[0], tokens[j]);
        }
      }
    }
  }
  return mt;
}

// Windows UTF-16 Names

#ifdef _WIN32
int ov_wfopen(const wchar_t* path, OggVorbis_File* vf) {
  int ret;
  FILE* f = _wfopen(path, L"rb");
  if (!f)
    return -1;

  ret = ov_open(f, vf, NULL, 0);
  if (ret)
    fclose(f);
  return ret;
}
#endif

class VorbisPlayer : public SimplePlayer {
 public:
  void set_format(OggVorbis_File* vf) {
    VorbisInfo* vi = ov_info(vf, -1);
    if (vi != nullptr) {
      format.channels = vi->channels;
      format.sample_rate = vi->rate;
      format.bits_per_sample = 16;
      format.total_samples = ov_pcm_total(vf, -1);
      format.duration = format.total_samples / format.sample_rate;
      format.avg_bytes_per_second =
          (format.channels * format.bits_per_sample * format.sample_rate) / 8;
    }
#ifdef _WIN32
    SimplePlayer::set_format();
#endif
  };

  void play(const fs::path& path) {
    OggVorbis_File vf;
    size_t buffer_size = default_buffer_size, read_bytes = 0;

    AudioResult result, secs;

#ifdef _WIN32
    result = ov_wfopen(path.native().c_str(), &vf);
#else
    result = ov_fopen(path.native().c_str(), &vf);
#endif

    if (result != 0) {
      TRACE_ERROR(Strings::format("Error opening file %d", result).c_str());
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    } else {
      TRACE_INFO(Strings::format("Opened %s", path.c_str()).c_str());
    }

    print_info(get_metadata(&vf));

    set_format(&vf);
    std::string buffer(buffer_size, '\0');

#ifdef _WIN32
    setupblocks();
#endif

    open();
    int is_bigendian = format.is_little_endian ? 0 : 1;
    int word_size = (format.bits_per_sample == 8) ? 1 : 2;

    uint64_t total_read_bytes = 0, seconds = 0;
    int width = (std::max)(80 * System::Console::get_console_width() / 100, 50);

    for (;;) {
      read_bytes = ov_read(&vf, &buffer[0], buffer_size, is_bigendian,
                           word_size, 1, &secs);

      if (read_bytes <= 0)
        break;
#ifdef _WIN32
      writeaudio(&buffer[0], read_bytes);
#elif __linux__
      frames = bytes_to_frames(read_bytes);
      writeaudio(&buffer[0], frames);
#endif
      total_read_bytes += read_bytes;
      if (seconds == (total_read_bytes / format.avg_bytes_per_second)) {
        System::Console::show_progress(seconds, total_duration(), width);
        seconds++;
      }
    }

#ifdef _WIN32
    freeblocks();
#endif
    cleanup(&vf);
    close();
    System::Console::print_color("Done Playing Song\n\n", Color::light_yellow);
  }

  void cleanup(OggVorbis_File* vf) { ov_clear(vf); }
};

Metadata get_metadata(const FLAC__StreamMetadata* metadata) {
  Metadata mt;
  auto tags = metadata->data.vorbis_comment;
  for (size_t i = 0; i < tags.num_comments; i++) {
    std::string comment(reinterpret_cast<char*>(tags.comments[i].entry));
    std::vector<std::string> tokens = Strings::split(comment, '=');
    if (tokens.size() > 1) {
      for (size_t j = 1; j < tokens.size(); j++) {
        add_meta(&mt, tokens[0], tokens[j]);
      }
    }
  }
  return mt;
}

class FlacPlayer : public SimplePlayer {
 private:
  uint64_t total_read_bytes, seconds;
  int width;

 public:
  void increment_read_bytes(uint64_t read_bytes = 0) {
    total_read_bytes += read_bytes;
  }
  void reset_console() {
    total_read_bytes = 0;
    seconds = 0;
    width = (std::max)(80 * System::Console::get_console_width() / 100, 50);
  }

  void update_console() {
    uint64_t current_duration = total_duration();
    seconds =
        (total_read_bytes * 1.0 / format.total_samples) * current_duration;
    System::Console::show_progress(seconds, current_duration, width);
  }
  void set_format(const FLAC__StreamMetadata* metadata) {
    format.sample_rate = metadata->data.stream_info.sample_rate;
    format.channels = metadata->data.stream_info.channels;

#ifdef _WIN32
    int bits_per_sample = metadata->data.stream_info.bits_per_sample;
    format.bits_per_sample = (bits_per_sample == 24) ? 32 : bits_per_sample;
#else
    format.bits_per_sample = metadata->data.stream_info.bits_per_sample;
#endif
    format.total_samples = metadata->data.stream_info.total_samples;
    format.duration = format.total_samples / format.sample_rate;
    format.avg_bytes_per_second =
        (format.channels * format.bits_per_sample * format.sample_rate) / 8;
    format.reserved = metadata->data.stream_info.bits_per_sample;
#ifdef _WIN32
    SimplePlayer::set_format();
#endif
  }

  void play(const fs::path& path) {
    reset_console();
    std::string extension = path.extension().string();
    bool is_ogg = Strings::equals_ignore_case(extension, ".oga");

    FLAC__bool ok = true;
    FLAC__StreamDecoder* decoder = 0;
    FLAC__StreamDecoderInitStatus init_status;

#if _WIN32
    setupblocks();
#endif
    if ((decoder = FLAC__stream_decoder_new()) == nullptr) {
      TRACE_ERROR("allocating decoder");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }

    FLAC__stream_decoder_set_md5_checking(decoder, true);
    FLAC__stream_decoder_set_metadata_respond(decoder,
                                              FLAC__METADATA_TYPE_STREAMINFO);
    FLAC__stream_decoder_set_metadata_respond(
        decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);
#ifdef _WIN32
    FILE* audio_file = _wfopen(path.native().c_str(), L"rb");
    if (audio_file == nullptr) {
      TRACE_ERROR("Failed to open file");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }
#endif
    if (is_ogg) {
#ifdef _WIN32

      init_status = FLAC__stream_decoder_init_ogg_FILE(
          decoder, audio_file, write_callback, metadata_callback,
          error_callback, this);
#else
      init_status = FLAC__stream_decoder_init_ogg_file(
          decoder, path.native().c_str(), write_callback, metadata_callback,
          error_callback, this);
#endif
    } else {
#ifdef _WIN32
      init_status = FLAC__stream_decoder_init_FILE(
          decoder, audio_file, write_callback, metadata_callback,
          error_callback, this);
#else
      init_status = FLAC__stream_decoder_init_file(
          decoder, path.native().c_str(), write_callback, metadata_callback,
          error_callback, this);
#endif
    }
    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
      TRACE_ERROR(
          Strings::format("initializing decoder: %s  %s",
                          FLAC__StreamDecoderInitStatusString[init_status],
                          path.c_str())
              .c_str());
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      ok = false;
    } else {
      TRACE_INFO(Strings::format("Opened %s", path.c_str()).c_str());
    }

    if (ok) {
      ok = FLAC__stream_decoder_process_until_end_of_stream(decoder);

      if (ok) {
        TRACE_SUCCESS(
            Strings::format(
                "decoding: %s   state: %s", ok ? "succeeded" : "FAILED",
                FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(
                    decoder)])
                .c_str());
      } else {
        TRACE_ERROR(
            Strings::format(
                "decoding: %s   state: %s", ok ? "succeeded" : "FAILED",
                FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(
                    decoder)])
                .c_str());
      }
    }

#ifdef _WIN32
    freeblocks();
    fclose(audio_file);
#endif
    cleanup(decoder);
    close();
    System::Console::print_color("Done Playing Song\n\n", Color::light_yellow);
  }

  void cleanup(FLAC__StreamDecoder* decoder) {
    FLAC__stream_decoder_delete(decoder);
  }

  static FLAC__StreamDecoderWriteStatus write_callback(
      const FLAC__StreamDecoder* decoder,
      const FLAC__Frame* frame,
      const FLAC__int32* const buffer[],
      void* client_data) {
    (void)decoder;

    FlacPlayer* player = reinterpret_cast<FlacPlayer*>(client_data);
    int32_t samples = frame->header.blocksize,
            channels = frame->header.channels;

    static int32_t
        buf[FLAC__MAX_BLOCK_SIZE * FLAC__MAX_CHANNELS * sizeof(int32_t)];

#ifdef _WIN32
    int bits_per_sample = player->bits_per_sample();
    int32_t decoded_size = samples * channels * (bits_per_sample / 8);
    int16_t* u16buf = reinterpret_cast<int16_t*>(buf);
    int32_t* u32buf = reinterpret_cast<int32_t*>(buf);
    int8_t* u8buf = reinterpret_cast<int8_t*>(buf);
#endif

    if (!(channels == 2 || channels == 1)) {
      TRACE_ERROR(
          Strings::format("This frame contains %d channels (should be 1 or 2)",
                          channels)
              .c_str());
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (buffer[0] == nullptr) {
      TRACE_ERROR("buffer[0] is null");
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (2 == channels && buffer[1] == nullptr) {
      TRACE_ERROR("buffer[1] is null");
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (player->format.reserved == 8 || player->format.reserved == 16 ||
        player->format.reserved == 24 || player->format.reserved == 32) {
      for (int32_t sample = 0, i = 0; sample < samples; sample++) {
        for (int32_t channel = 0; channel < channels; channel++, i++) {
#ifdef _WIN32
          switch (player->format.reserved) {
            case 8:
              u8buf[i] = static_cast<int8_t>(buffer[channel][sample] & 0xff);
              break;
            case 16:
              u16buf[i] =
                  static_cast<int16_t>(buffer[channel][sample] & 0xffff);
              break;
            case 24:
              u32buf[i] = static_cast<int32_t>(buffer[channel][sample] * 0x100);
              break;
            case 32:
              u32buf[i] = static_cast<int32_t>(buffer[channel][sample]);
              break;
          }
#elif __linux__
          buf[i] = static_cast<int32_t>(buffer[channel][sample]);
#endif
        }
      }

#ifdef _WIN32
      player->writeaudio(reinterpret_cast<LPSTR>(buf), decoded_size);

#elif __linux
      player->writeaudio(buf, samples);
#endif
      player->increment_read_bytes(samples);
      player->update_console();
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
  }

  static void metadata_callback(const FLAC__StreamDecoder* decoder,
                                const FLAC__StreamMetadata* metadata,
                                void* client_data) {
    (void)decoder;
    FlacPlayer* player = reinterpret_cast<FlacPlayer*>(client_data);

    switch (metadata->type) {
      case FLAC__METADATA_TYPE_STREAMINFO: {
        player->set_format(metadata);
        player->open();
      } break;
      case FLAC__METADATA_TYPE_VORBIS_COMMENT: {
        print_info(get_metadata(metadata));
      } break;
      default:
        break;
    }
  }

  static void error_callback(const FLAC__StreamDecoder* decoder,
                             FLAC__StreamDecoderErrorStatus status,
                             void* client_data) {
    (void)decoder;
    (void)client_data;
    TRACE_ERROR(Strings::format("Got error callback: %s",
                                FLAC__StreamDecoderErrorStatusString[status])
                    .c_str());
    System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
  }
};

Metadata get_metadata(OggOpusFile* op_file) {
  Metadata mt;
  const OpusTags* tags = op_tags(op_file, -1);
  if (tags != nullptr) {
    for (int i = 0; i < tags->comments; i++) {
      size_t comment_length = tags->comment_lengths[i];
      std::string comment(comment_length + 1, '\0');
      strncpy(&comment[0], tags->user_comments[i], comment_length);
      std::vector<std::string> tokens = Strings::split(comment, '=');
      if (tokens.size() > 1) {
        for (size_t j = 1; j < tokens.size(); j++) {
          add_meta(&mt, tokens[0], tokens[j]);
        }
      }
    }
  }
  return mt;
}
class OpusPlayer : public SimplePlayer {
 public:
  void set_format(OggOpusFile* op_file) {
    const OpusHead* head = op_head(op_file, -1);
    if (head != nullptr) {
      format.sample_rate = head->input_sample_rate;
      format.channels = head->channel_count;
      format.bits_per_sample = 16;
      format.total_samples = static_cast<int>(op_pcm_total(op_file, -1));
      format.duration = format.total_samples / 48000;
      format.avg_bytes_per_second =
          (format.channels * format.bits_per_sample * 48000) / 8;
    }
#ifdef _WIN32
    SimplePlayer::set_format();
#endif
  }
  void play(const fs::path& path) {
    AudioResult result;
#ifdef _WIN32
    std::string win_path = Strings::to_string(path.native().c_str());
    OggOpusFile* op_file = op_open_file(win_path.c_str(), &result);
#else
    OggOpusFile* op_file = op_open_file(path.native().c_str(), &result);
#endif
    if (result) {
      TRACE_ERROR("Failed to open File");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    } else {
      TRACE_INFO(Strings::format("Opened %s", path.c_str()).c_str());
    }

    set_format(op_file);

    print_info(get_metadata(op_file));

    static opus_int16 buf[0x1000];

    int read_bytes;

#ifdef _WIN32
    setupblocks();
#endif
    open();

    uint64_t total_read_bytes = 0, seconds = 0;
    int width = (std::max)(80 * System::Console::get_console_width() / 100, 50);

    for (;;) {
      read_bytes = op_read(op_file, buf, 0x1000, nullptr);
      if (read_bytes <= 0) {
        break;
      }
#ifdef _WIN32
      writeaudio(reinterpret_cast<LPSTR>(buf), read_bytes * channels() * 2);
#elif __linux__
      frames = bytes_to_frames(read_bytes * channels() * 2);
      writeaudio(&buf[0], frames);
#endif
      total_read_bytes += (read_bytes * channels() * 2);
      if (seconds == (total_read_bytes / format.avg_bytes_per_second)) {
        System::Console::show_progress(seconds, total_duration(), width);
        seconds++;
      }
    }
#ifdef _WIN32
    freeblocks();
#endif
    close();
    System::Console::print_color("Done Playing Song\n\n", Color::light_yellow);
  }
};

Metadata get_metadata(const AIFF_Ref ref) {
  Metadata mt;
  char* attribute;
  attribute = AIFF_GetAttribute(ref, AIFF_NAME);
  if (attribute) {
    mt.title.append(attribute);
    free(attribute);
  }
  attribute = AIFF_GetAttribute(ref, AIFF_AUTH);
  if (attribute) {
    mt.artist.append(attribute);
    free(attribute);
  }
  attribute = AIFF_GetAttribute(ref, AIFF_ANNO);
  if (attribute) {
    mt.comment.append(attribute);
    free(attribute);
  }
  return mt;
}
class AIFFPlayer : public SimplePlayer {
 public:
  void set_format(const AIFF_Ref ref) {
    if (ref) {
      uint64_t total_samples;
      int channels;
      double sample_rate;
      int bits_per_sample;
      int segmentSize;

      if (AIFF_GetAudioFormat(ref, &total_samples, &channels, &sample_rate,
                              &bits_per_sample, &segmentSize) < 1) {
        AIFF_Close(ref);
        TRACE_ERROR("Failed to open File");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
      format.sample_rate = static_cast<int>(sample_rate);
      format.channels = channels;
      format.bits_per_sample = bits_per_sample;
      format.total_samples = total_samples;
      format.duration = format.total_samples / format.sample_rate;
      format.avg_bytes_per_second =
          (format.channels * format.bits_per_sample * format.sample_rate) / 8;
#ifdef _WIN32
      SimplePlayer::set_format();
#endif
    }
  }
  void play(const fs::path& path) {
    AIFF_Ref ref;

#ifdef _WIN32
    ref = AIFF_OpenFileW(path.native().c_str(), F_RDONLY);
#else
    ref = AIFF_OpenFile(path.native().c_str(), F_RDONLY);
#endif
    if (ref) {
      TRACE_SUCCESS("File opened successfully.");
    } else {
      TRACE_ERROR("Failed to open File");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }
    set_format(ref);
    int read_bytes;
    print_info(get_metadata(ref));

#ifdef _WIN32
    setupblocks();
#endif
    open();
    constexpr int buffer_size = 0x1000;
    std::string buffer(buffer_size, '\0');
    ;

    uint64_t total_read_bytes = 0, seconds = 0;
    int width = (std::max)(80 * System::Console::get_console_width() / 100, 50);

    for (;;) {
      read_bytes = AIFF_ReadSamples(ref, &buffer[0], buffer_size);
      if (read_bytes <= 0) {
        break;
      }
#ifdef _WIN32
      writeaudio(reinterpret_cast<LPSTR>(&buffer[0]), read_bytes);
#elif __linux__
      frames = bytes_to_frames(read_bytes);
      writeaudio(&buffer[0], frames);
#endif
      total_read_bytes += read_bytes;
      if (seconds == (total_read_bytes / format.avg_bytes_per_second)) {
        System::Console::show_progress(seconds, total_duration(), width);
        seconds++;
      }
    }
#ifdef _WIN32
    freeblocks();
#endif
    close();
    if (ref)
      AIFF_Close(ref);

    System::Console::print_color("Done Playing Song\n\n", Color::light_yellow);
  }
};

int64_t to_integer(char a[]) {
  int64_t n = 0;
  n = (((int64_t)a[0] << 56) & 0xFF00000000000000U) |
      (((int64_t)a[1] << 48) & 0x00FF000000000000U) |
      (((int64_t)a[2] << 40) & 0x0000FF0000000000U) |
      (((int64_t)a[3] << 32) & 0x000000FF00000000U) |
      ((a[4] << 24) & 0x00000000FF000000U) |
      ((a[5] << 16) & 0x0000000000FF0000U) |
      ((a[6] << 8) & 0x000000000000FF00U) | (a[7] & 0x00000000000000FFU);
  return n;
}
#if defined(__GNUC__) || defined(__clang__)
#define PACKED(kclass) kclass __attribute__((__packed__))
#else
#define PACKED(kclass) __pragma(pack(push, 1)) kclass __pragma(pack(pop))
#endif

PACKED(struct CAFChunkHeader {
  uint32_t chunk_type;
  int64_t chunk_size;
});

PACKED(struct CAFFileHeader {
  uint32_t file_type;
  uint16_t file_version;
  uint16_t flags;
});

PACKED(struct CAFPacketTableHeader {
  int64_t packets;
  int64_t valid_frames;
  int32_t priming_frames;
  int32_t remainder_frames;
});

PACKED(struct CAFAudioDescription {
  double sample_rate;
  uint32_t format_id;
  uint32_t format_flags;
  uint32_t bytes_per_packet;
  uint32_t frames_per_packet;
  uint32_t channels;
  uint32_t bits_per_sample;
});

std::string to_string(CAFAudioDescription& description) {
  return "Sample Rate: " + std::to_string(description.sample_rate) + "\n" +
         "Format Id " + std::string((char*)&description.format_id, 4) + "\n" +
         "Format Flags " + std::to_string(description.format_flags) + "\n" +
         "Bits Per Channel " + std::to_string(description.bits_per_sample) +
         "\nBytes Per Packet " + std::to_string(description.bytes_per_packet) +
         "\nFrames Per Packet " +
         std::to_string(description.frames_per_packet) + "\n" + "Channels " +
         std::to_string(description.channels) + "\n";
}

std::string to_string(CAFPacketTableHeader& pkt_table) {
  return "Packets size " + std::to_string(pkt_table.packets) +
         "\nValid Frames " + std::to_string(pkt_table.valid_frames) +
         "\nPriming Frames " + std::to_string(pkt_table.priming_frames) +
         "\nRemainder Frames " + std::to_string(pkt_table.remainder_frames) +
         "\n";
}

void log_chunk(CAFChunkHeader& header) {
  TRACE_INFO(Strings::format("CHUNK_TYPE: %s\n CHUNK_SIZE: %d\n",
                             std::string((char*)&header.chunk_type, 4).c_str(),
                             Swap64BtoN(header.chunk_size))
                 .c_str());
}
enum class Source : int {
  k16BitSourceData = 1,
  k20BitSourceData = 2,
  k24BitSourceData = 3,
  k32BitSourceData = 4
};

enum class CAFAudioFormat : uint32_t {
  kAudioFormatLinearPCM = 'lpcm',
  kAudioFormatAC3 = 'ac-3',
  kAudioFormat60958AC3 = 'cac3',
  kAudioFormatAppleIMA4 = 'ima4',
  kAudioFormatMPEG4AAC = 'aac ',
  kAudioFormatMPEG4CELP = 'celp',
  kAudioFormatMPEG4HVXC = 'hvxc',
  kAudioFormatMPEG4TwinVQ = 'twvq',
  kAudioFormatMACE3 = 'MAC3',
  kAudioFormatMACE6 = 'MAC6',
  kAudioFormatULaw = 'ulaw',
  kAudioFormatALaw = 'alaw',
  kAudioFormatQDesign = 'QDMC',
  kAudioFormatQDesign2 = 'QDM2',
  kAudioFormatQUALCOMM = 'Qclp',
  kAudioFormatMPEGLayer1 = '.mp1',
  kAudioFormatMPEGLayer2 = '.mp2',
  kAudioFormatMPEGLayer3 = '.mp3',
  kAudioFormatTimeCode = 'time',
  kAudioFormatMIDIStream = 'midi',
  kAudioFormatParameterValueStream = 'apvs',
  kAudioFormatAppleLossless = 'alac',
  kAudioFormatMPEG4AAC_HE = 'aach',
  kAudioFormatMPEG4AAC_LD = 'aacl',
  kAudioFormatMPEG4AAC_ELD = 'aace',
  kAudioFormatMPEG4AAC_ELD_SBR = 'aacf',
  kAudioFormatMPEG4AAC_ELD_V2 = 'aacg',
  kAudioFormatMPEG4AAC_HE_V2 = 'aacp',
  kAudioFormatMPEG4AAC_Spatial = 'aacs',
  kAudioFormatAMR = 'samr',
  kAudioFormatAMR_WB = 'sawb',
  kAudioFormatAudible = 'AUDB',
  kAudioFormatiLBC = 'ilbc',
  kAudioFormatDVIIntelIMA = 0x6D730011,
  kAudioFormatMicrosoftGSM = 0x6D730031,
  kAudioFormatAES3 = 'aes3',
  kAudioFormatEnhancedAC3 = 'ec-3',
  kAudioFormatFLAC = 'flac',
  kAudioFormatOpus = 'opus'
};

static uint32_t kStreamDescription = 'desc';
static uint32_t kAudioData = 'data';
static uint32_t kChannelLayout = 'chan';
static uint32_t kMagicCookie = 'kuki';
static uint32_t kPacketTable = 'pakt';
static uint32_t kFreeTable = 'free';

#define kMaxBERSize 5
#define kCAFFdataChunkEditsSize 4
#define kMinCAFFPacketTableHeaderSize 24

uint32_t decode_var_int(uint8_t* input_buffer, int32_t* num_bytes) {
  uint32_t result = 0;
  uint8_t data;
  int32_t size = 0;
  do {
    data = input_buffer[size];
    result = (result << 7) | (data & 0x7F);
    if (++size > 5) {
      size = 0xFFFFFFFF;
      return 0;
    }
  } while (((data & 0x80) != 0) && (size <= *num_bytes));

  *num_bytes = size;
  return result;
}
int find_caff_chunk(std::ifstream& f,
                    CAFChunkHeader& header,
                    uint32_t chunk_type) {
  // reset begin
  f.seekg(sizeof(CAFFileHeader), f.beg);
  int64_t chunk_size = 0;
  int read_bytes = 0;
  for (;;) {
    read_bytes = read_data(f, &header, sizeof(CAFChunkHeader));
    if (header.chunk_type == SWAP_UINT32(chunk_type) || read_bytes <= 0) {
      break;
    } else {
      chunk_size = Swap64BtoN(header.chunk_size);
      f.ignore(chunk_size);
    }
    log_chunk(header);
  }
  return static_cast<int>(f.tellg());
}

class CAFPlayer : public SimplePlayer {
 public:
  void play(const fs::path& path) {
    std::ifstream caff_file(path.native(), std::ifstream::binary);
    if (caff_file) {
      char caff_type[8];
      read_data(caff_file, caff_type, 8);
      if (Strings::details::basic_strncmp(caff_type, "caff", 4) == 0) {
        CAFChunkHeader header;
        int64_t cookie_size, pkt_size, data_size;

        std::streampos current_pkt_pos, current_data_pos;

        find_caff_chunk(caff_file, header, kStreamDescription);
        log_chunk(header);

        CAFAudioDescription description;
        read_data(caff_file, &description, sizeof(CAFAudioDescription));
        description.sample_rate = SwapFloat64BtoN(description.sample_rate);
        description.format_id = Swap32BtoN(description.format_id);
        description.format_flags = Swap32BtoN(description.format_flags);
        description.bytes_per_packet = Swap32BtoN(description.bytes_per_packet);
        description.frames_per_packet =
            Swap32BtoN(description.frames_per_packet);
        description.channels = Swap32BtoN(description.channels);
        description.bits_per_sample = Swap32BtoN(description.bits_per_sample);

        if (description.format_id != kALACFormatAppleLossless) {
          if ((description.format_flags & 0x02) == 0x02) {
            description.format_flags &= 0xfffffffc;
          } else {
            description.format_flags |= 0x02;
          }
        }

        if ((description.format_id != kALACFormatLinearPCM) ||
            ((description.format_flags & kALACFormatFlagIsFloat) != 0)) {
          description.bits_per_sample = 16;
        } else {
          switch (description.format_flags) {
            case static_cast<int>(Source::k16BitSourceData):
              description.bits_per_sample = 16;
              break;
            case static_cast<int>(Source::k20BitSourceData):
              description.bits_per_sample = 20;
              break;
            case static_cast<int>(Source::k24BitSourceData):
              description.bits_per_sample = 24;
              break;
            case static_cast<int>(Source::k32BitSourceData):
              description.bits_per_sample = 32;
              break;
            default:
              break;
          }
        }

        TRACE_INFO(
            Strings::format("DESCRIPTION: %s\n", to_string(description).c_str())
                .c_str());

        int32_t read_bytes = 0, num_bytes = 0;
        uint32_t num_frames = 0;

        int bytes_per_frame =
            (description.channels * ((description.bits_per_sample) >> 3));
        format.sample_rate = static_cast<int>(description.sample_rate);
        format.channels = description.channels;
        format.bits_per_sample = description.bits_per_sample;

#ifdef _WIN32
        SimplePlayer::set_format();
#endif

#ifdef _WIN32
        setupblocks();
#endif
        open();

        current_data_pos = find_caff_chunk(caff_file, header, kAudioData);
        current_data_pos += sizeof(uint32_t);  // mEditCount
        data_size = Swap64BtoN(header.chunk_size);
        data_size -= sizeof(uint32_t);
        log_chunk(header);

        int32_t input_buffer_size = description.channels *
                                        (description.bits_per_sample >> 3) *
                                        description.frames_per_packet +
                                    kALACMaxEscapeHeaderBytes;
        std::string read_buffer(input_buffer_size, '\0');

        switch (description.format_id) {
            // CAFAudioFormat::kAudioFormatLinearPCM:
          case static_cast<int>(CAFAudioFormat::kAudioFormatLinearPCM): {
            for (;;) {
              std::istream& is_ok =
                  caff_file.read(&read_buffer[0], input_buffer_size);
              read_bytes = static_cast<int>(is_ok.gcount());
              if (read_bytes <= 0) {
                break;
              }

#ifdef _WIN32
              writeaudio(reinterpret_cast<LPSTR>(&read_buffer[0]), read_bytes);
#elif __linux__
              frames = bytes_to_frames(read_bytes);
              writeaudio(&read_buffer[0], frames);
#endif
            }
          } break;
          // CAFAudioFormat::kAudioFormatAppleIMA4
          case static_cast<int>(CAFAudioFormat::kAudioFormatAppleIMA4): {
            // CAFPacketTableHeader pkt_header;
            //
            // current_pkt_pos = find_caff_chunk(caff_file, header,
            // kPacketTable); read_data(caff_file, &pkt_header,
            // sizeof(CAFPacketTableHeader));
            //
            // pkt_header.packets = Swap64BtoN(pkt_header.packets);
            // pkt_header.valid_frames = Swap64BtoN(pkt_header.valid_frames);
            // pkt_header.priming_frames =
            // Swap32BtoN(pkt_header.priming_frames);
            // pkt_header.remainder_frames =
            // Swap32BtoN(pkt_header.remainder_frames);
            //
            // current_pkt_pos += sizeof(CAFPacketTableHeader);
            // pkt_size = Swap64BtoN(header.chunk_size);
            // log_chunk(header);
            //
            // int last_pkt_frames =
            //         description.frames_per_packet -
            //         pkt_header.remainder_frames,
            //     total_read_bytes = 0;
            // int audio_data_size = (pkt_header.valid_frames + last_pkt_frames)
            // *
            //                       description.bytes_per_packet /
            //                       description.frames_per_packet;
            //
            // caff_file.seekg(current_data_pos, caff_file.beg);
            //
            // std::string write_buffer(
            //     description.frames_per_packet * sizeof(int16_t), '\0');

            // unsigned char *packet = nullptr;
            // short *output = nullptr;

            //           for (;;) {
            //
            //             // seek data
            //             std::istream &is_ok = caff_file.read(&read_buffer[0],
            //             description.bytes_per_packet); read_bytes =
            //             static_cast<int>(is_ok.gcount()); if (read_bytes <= 0
            //             || total_read_bytes > audio_data_size) {
            //               break;
            //             }
            //             total_read_bytes += read_bytes;
            //
            //             CodecState state;
            //             memset(&state, 0, sizeof(state));
            //
            //
            //             decode(&state, (uint8_t*)&read_buffer[0],
            //             description.frames_per_packet,
            //             (int16_t*)&write_buffer[0]);
            //
            // #ifdef _WIN32
            //             writeaudio(reinterpret_cast<LPSTR>(&write_buffer[0]),
            //                        description.bytes_per_packet);
            // #elif __linux__
            //             frames = bytes_to_frames(read_bytes);
            //             writeaudio(&write_buffer[0], frames);
            // #endif
            //           }

            TRACE_ERROR("CAFAudioFormat::kAudioFormatAppleIMA4");
            System::Process::exit_process(
                static_cast<int>(AudioStatus::kIoError));
          }; break;
          // CAFAudioFormat::kAudioFormatAppleLossless:
          case static_cast<int>(CAFAudioFormat::kAudioFormatAppleLossless): {
            find_caff_chunk(caff_file, header, kMagicCookie);
            cookie_size = ((char*)&header.chunk_size)[7];
            std::string magic_cookie(cookie_size, '\0');
            read_data(caff_file, &magic_cookie.at(0), magic_cookie.size());
            log_chunk(header);

            CAFPacketTableHeader pkt_header;

            current_pkt_pos = find_caff_chunk(caff_file, header, kPacketTable);
            read_data(caff_file, &pkt_header, sizeof(CAFPacketTableHeader));
            current_pkt_pos += sizeof(CAFPacketTableHeader);
            pkt_size = Swap64BtoN(header.chunk_size);
            log_chunk(header);

            std::unique_ptr<ALACDecoder> decoder =
                std::make_unique<ALACDecoder>();

            int32_t out_buffer_size =
                input_buffer_size - kALACMaxEscapeHeaderBytes;

            BitBuffer input_buffer;

            std::string write_buffer(out_buffer_size, '\0');

            decoder->Init(&magic_cookie.at(0), cookie_size);

            BitBufferInit(&input_buffer, (uint8_t*)&read_buffer.at(0),
                          input_buffer_size);

            for (;;) {
              // seek table

              caff_file.seekg(current_pkt_pos, caff_file.beg);
              num_bytes = read_data(caff_file, &read_buffer[0], kMaxBERSize);
              input_buffer_size =
                  decode_var_int((uint8_t*)&read_buffer.at(0), &num_bytes);
              current_pkt_pos += num_bytes;

              // seek data
              caff_file.seekg(current_data_pos, caff_file.beg);
              current_data_pos += input_buffer_size;

              std::istream& is_ok =
                  caff_file.read(&read_buffer[0], input_buffer_size);
              read_bytes = static_cast<int>(is_ok.gcount());
              if (read_bytes <= 0) {
                break;
              }
              decoder->Decode(&input_buffer, (uint8_t*)&write_buffer.at(0),
                              description.frames_per_packet,
                              description.channels, &num_frames);
              read_bytes = num_frames * bytes_per_frame;

#ifdef _WIN32
              writeaudio(reinterpret_cast<LPSTR>(&write_buffer[0]), read_bytes);
#elif __linux__
              frames = bytes_to_frames(read_bytes);
              writeaudio(&write_buffer[0], frames);
#endif

              BitBufferReset(&input_buffer);
            }

            decoder.reset();
          }

          break;
          default: {
            TRACE_ERROR("Not supported format");
            System::Process::exit_process(
                static_cast<int>(AudioStatus::kIoError));
          }
        }
#ifdef _WIN32
        freeblocks();
#endif
        close();
      } else {
        TRACE_ERROR("Wrong format");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
    } else {
      TRACE_ERROR("Failed to open File");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }
  }
};

PACKED(struct MediaHeaderEntryV1 {
  uint64_t created;
  uint64_t modified;
  uint32_t time_scale;
  uint64_t duration;
  uint16_t language;
  uint16_t quality;
});
PACKED(struct MediaHeaderEntryV0 {
  uint32_t created;
  uint32_t modified;
  uint32_t time_scale;
  uint32_t duration;
  uint16_t language;
  uint16_t quality;
});

PACKED(struct Box {
  uint32_t box_size;
  uint32_t box_type;
});

PACKED(struct AudioSampleEntry {
  uint32_t entry_size;
  uint32_t entry_type;
  uint8_t reserved1[6];
  uint16_t data_reference_index;
  uint32_t reserved2[2];
  uint16_t channels;
  uint16_t bits_per_sample;
  uint16_t pre_defined;
  uint16_t reserved3;
  uint16_t samplerate_high;
  uint16_t samplerate_low;
});

PACKED(struct MP4Info {
  uint32_t info_size;
  uint32_t info_id;
  uint32_t Version_flags;
});

void log_chunk(Box& hdr) {
  TRACE_INFO(Strings::format("type is %s\n chunk size is %d\n",
                             std::string((char*)&hdr.box_type, 4).c_str(),
                             SWAP_UINT32(hdr.box_size))
                 .c_str());
}

void log_config(ALACSpecificConfig* config) {
  if (config != nullptr) {
    TRACE_INFO(
        Strings::format(
            "frameLength %d\ncompatibleVersion %d\nbitDepth %d\npb %d\nmb "
            "%d\nkb %d\nnumChannels % d\nmaxRun % d\nmaxFrameBytes % "
            "d\navgBitRate % d\nsampleRate % d\n ",
            SWAP_UINT32(config->frameLength),
            static_cast<int>(config->compatibleVersion),
            static_cast<int>(config->bitDepth), static_cast<int>(config->pb),
            static_cast<int>(config->mb), static_cast<int>(config->kb),
            static_cast<int>(config->numChannels), SWAP_UINT16(config->maxRun),
            SWAP_UINT32(config->maxFrameBytes), SWAP_UINT32(config->avgBitRate),
            SWAP_UINT32(config->sampleRate))
            .c_str());
  }
}

void find_atom(std::ifstream& m4a_file,
               const std::vector<uint32_t>& atoms,
               Box* box) {
  m4a_file.seekg(0, m4a_file.beg);
  int index = 0, read_bytes = 0;
  while (index < static_cast<int>(atoms.size())) {
    read_bytes = read_data(m4a_file, box, sizeof(Box));
    if (read_bytes <= 0) {
      TRACE_ERROR("Failed to read");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    } else {
      log_chunk(*box);
      if (atoms[index] == (SWAP_UINT32(box->box_type))) {
        index++;
      } else {
        m4a_file.ignore(SWAP_UINT32(box->box_size) - read_bytes);
      }
    }
  }
}
PACKED(struct SampleChunk {
  uint32_t first_chunk;
  uint32_t samples_per_chunk;
  uint32_t sample_description_index;
});

constexpr uint32_t kFTYPE = 'ftyp';
constexpr uint32_t kALAC = 'alac';
constexpr uint32_t kMP4a = 'mp4a';

PACKED(struct ES_Descriptor {
  uint8_t tag;
  uint8_t optional[3];
  uint8_t length;
  uint16_t ES_ID;
  uint8_t streamInfo;
});

PACKED(struct DecoderConfigDescriptor {
  uint8_t tag;
  uint8_t optional[3];
  uint8_t length;
  uint8_t objectTypeIndication;
  uint8_t streaminfo;
  uint8_t bufferSizeDB[3];
  uint32_t maxBitrate;
  uint32_t avgBitrate;
});

PACKED(struct DecoderSpecificInfo {
  uint8_t tag;
  uint8_t optional[3];
  uint8_t length;
});

std::string get_codec(const std::string& input) {
  static const char* const lut = "0123456789ABCDEF";
  size_t len = input.length();

  std::string output;
  output.reserve(6 * len);
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = input[i];
    output.push_back('0');
    output.push_back('x');
    output.push_back(lut[c >> 4]);
    output.push_back(lut[c & 15]);
    output.push_back(' ');
  }
  return output;
}

void log_entry(const AudioSampleEntry& entry) {
  TRACE_INFO(
      Strings::format(
          "Size of Sample Entry %d\nEntry type is %s\nEntry Size is %d\nSample "
          "Size is %d\nChannel Count %d\nSample Rate %d\n",
          sizeof(AudioSampleEntry),
          std::string((char*)&entry.entry_type, 4).c_str(),
          SWAP_UINT32(entry.entry_size), SWAP_UINT16(entry.bits_per_sample),
          SWAP_UINT16(entry.channels), SWAP_UINT16(entry.samplerate_high))
          .c_str());
}

class M4APlayer : public SimplePlayer {
 public:
  void play(const fs::path& path) {
    std::ifstream m4a_file(path.c_str(), std::ifstream::binary);
    if (m4a_file) {
      Box header;
      char version_flags[4];
      uint32_t entries_count;
      AudioSampleEntry entry;
      std::vector<uint32_t> atoms;

      int read_bytes = read_data(m4a_file, &header, sizeof(Box));

      if (header.box_type == SWAP_UINT32(kFTYPE)) {
        TRACE_SUCCESS("Right Header");
        log_chunk(header);
      } else {
        TRACE_ERROR("Failed to read");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }

      atoms = {'moov', 'trak', 'mdia', 'mdhd'};
      find_atom(m4a_file, atoms, &header);
      read_data(m4a_file, version_flags, 4);
      int version = static_cast<int>(version_flags[0]);

      if (version == 0) {
        MediaHeaderEntryV0 entry0;
        read_data(m4a_file, &entry0, sizeof(entry0));
        format.sample_rate = SWAP_UINT32(entry0.time_scale);
        TRACE_INFO(
            Strings::format("SAMPLE_RATE: %d\n", format.sample_rate).c_str());
      } else if (version == 1) {
        MediaHeaderEntryV1 entry1;
        read_data(m4a_file, &entry1, sizeof(entry1));
        format.sample_rate = SWAP_UINT32(entry1.time_scale);
        TRACE_INFO(
            Strings::format("SAMPLE_RATE: %d\n", format.sample_rate).c_str());
      } else {
        TRACE_ERROR("Unknown Version");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
      atoms = {'moov', 'trak', 'mdia', 'minf', 'stbl', 'stsz'};
      find_atom(m4a_file, atoms, &header);
      read_data(m4a_file, version_flags, 4);

      uint32_t sample_size, sample_count, entry_size;
      read_data(m4a_file, &sample_size, 4);
      read_data(m4a_file, &sample_count, 4);
      sample_size = SWAP_UINT32(sample_size);
      sample_count = SWAP_UINT32(sample_count);
      std::vector<uint32_t> sample_sizes;
      if (sample_size == 0) {
        for (int i = 0; i < static_cast<int>(sample_count); i++) {
          read_data(m4a_file, &entry_size, 4);
          sample_sizes.push_back(SWAP_UINT32(entry_size));
        }
      }
      atoms = {'moov', 'trak', 'mdia', 'minf', 'stbl', 'stsd'};

      find_atom(m4a_file, atoms, &header);
      read_data(m4a_file, version_flags, 4);
      read_data(m4a_file, &entries_count, 4);
      read_data(m4a_file, &entry, sizeof(AudioSampleEntry));
      log_entry(entry);

      format.bits_per_sample = SWAP_UINT16(entry.bits_per_sample);
      format.channels = SWAP_UINT16(entry.channels);
#ifdef _WIN32
      SimplePlayer::set_format();
#endif

#ifdef _WIN32
      setupblocks();
#endif
      open();

      uint32_t entry_type = SWAP_UINT32(entry.entry_type);
      bool is_alac = entry_type == kALAC;
      bool is_mp4a = entry_type == kMP4a;

      MP4Info info;

      if (is_alac || is_mp4a) {
        if (is_alac) {
          std::unique_ptr<ALACDecoder> decoder =
              std::make_unique<ALACDecoder>();
          read_data(m4a_file, &info, sizeof(MP4Info));
          std::string info_id_content{(char*)&info.info_id, 4};
          TRACE_INFO(Strings::format("Info size: %d id: %s",
                                     SWAP_UINT32(info.info_size),
                                     info_id_content.c_str())
                         .c_str());
          int header_size = SWAP_UINT32(header.box_size);
          header_size -=
              (8 + sizeof(Box) + sizeof(AudioSampleEntry) + sizeof(MP4Info));
          std::string magic_cookie("");
          if (header_size == 24 || header_size == 48) {
            magic_cookie.resize(header_size, '\0');
            read_data(m4a_file, &magic_cookie[0], magic_cookie.size());
          } else {
            TRACE_ERROR("Magic Size is small");
            System::Process::exit_process(
                static_cast<int>(AudioStatus::kIoError));
          }
          ALACSpecificConfig* config = (ALACSpecificConfig*)(&magic_cookie[0]);
          log_config(config);

          decoder->Init(&magic_cookie.at(0), magic_cookie.size());

          atoms = {'mdat'};
          find_atom(m4a_file, atoms, &header);
          constexpr uint32_t kMaxByteDepth = 4;
          constexpr uint32_t kMaxSamplesPerFrame = 0x3000;
          constexpr uint32_t kMaxChannels = 2;

          constexpr int input_buffer_size =
              kMaxSamplesPerFrame * kMaxByteDepth * kMaxChannels +
              kALACMaxEscapeHeaderBytes;
          constexpr int out_buffer_size =
              kMaxSamplesPerFrame * kMaxByteDepth * kMaxChannels +
              kALACMaxEscapeHeaderBytes;

          uint32_t frame_length = SWAP_UINT32(config->frameLength);

          BitBuffer input_buffer;
          std::string read_buffer(input_buffer_size, '\0');
          std::string write_buffer(out_buffer_size, '\0');

          BitBufferInit(&input_buffer, (uint8_t*)&read_buffer.at(0),
                        input_buffer_size);

          uint32_t num_frames = 0;

          atoms = {'mdat'};
          find_atom(m4a_file, atoms, &header);
          for (int i = 0; i < static_cast<int>(sample_sizes.size()); i++) {
            std::istream& is_ok =
                m4a_file.read(&read_buffer[0], sample_sizes[i]);
            read_bytes = static_cast<int>(is_ok.gcount());
            if (read_bytes <= 0) {
              break;
            }

            decoder->Decode(&input_buffer, (uint8_t*)&write_buffer.at(0),
                            frame_length, format.channels, &num_frames);
            read_bytes =
                num_frames * (format.bits_per_sample / 8) * format.channels;
#ifdef _WIN32
            writeaudio(reinterpret_cast<LPSTR>(&write_buffer[0]), read_bytes);
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            BitBufferReset(&input_buffer);
          }
        } else {
          constexpr int output_size = 0x40000;
          uint8_t output_buf[output_size];
          int16_t decode_buf[output_size];
          std::string read_buffer(output_size, '\0');
          uint8_t* data = (uint8_t*)&read_buffer[0];
          read_data(m4a_file, &header, sizeof(Box));
          read_data(m4a_file, version_flags, 4);
          log_chunk(header);

          ES_Descriptor desc;

          read_data(m4a_file, &desc, sizeof(desc));
          TRACE_INFO(Strings::format("ES_Descriptor TAG: %d TAG_LENGTH: %d",
                                     static_cast<int>(desc.tag),
                                     static_cast<int>(desc.length))
                         .c_str());

          DecoderConfigDescriptor cdesc;

          read_data(m4a_file, &cdesc, sizeof(cdesc));
          TRACE_INFO(
              Strings::format("DecoderConfigDescriptor TAG: %d TAG_LENGTH: %d",
                              static_cast<int>(cdesc.tag),
                              static_cast<int>(cdesc.length))
                  .c_str());

          DecoderSpecificInfo csinfo;

          read_data(m4a_file, &csinfo, sizeof(csinfo));
          TRACE_INFO(
              Strings::format("DecoderSpecificInfo TAG: %d TAG_LENGTH: %d",
                              static_cast<int>(csinfo.tag),
                              static_cast<int>(csinfo.length))
                  .c_str());

          uint32_t length = static_cast<int>(csinfo.length);

          std::string codec(length, '\0');
          read_data(m4a_file, &codec[0], codec.size());

          TRACE_INFO(
              Strings::format("CODEC: %s", get_codec(codec).c_str()).c_str());

          atoms = {'mdat'};
          find_atom(m4a_file, atoms, &header);
          CStreamInfo* cstream_info = nullptr;

          UINT valid;
          HANDLE_AACDECODER handle;
          AAC_DECODER_ERROR err;
          int frame_length = 0;

          unsigned char codecdata[64] = {0};
          uint32_t codecdata_len = (std::min)(64, (int)codec.size());
          for (int i = 0; i < static_cast<int>(codecdata_len); i++) {
            codecdata[i] = codec.at(i);
          }

          uint8_t* ascData[] = {codecdata};
          uint32_t ascSize[] = {codecdata_len};

          handle = aacDecoder_Open(TT_MP4_RAW, 1);
          err = aacDecoder_ConfigRaw(handle, ascData, ascSize);
          if (err != AAC_DEC_OK) {
            TRACE_ERROR("Unable to decode the ASC\n");
            System::Process::exit_process(
                static_cast<int>(AudioStatus::kIoError));
          } else {
            TRACE_SUCCESS("init worked\n");
          }

          if (aacDecoder_SetParam(handle, AAC_CONCEAL_METHOD, 1) !=
              AAC_DEC_OK) {
            TRACE_ERROR("Unable to set error concealment method\n");
          }
          if (aacDecoder_SetParam(handle, AAC_PCM_LIMITER_ENABLE, 0) !=
              AAC_DEC_OK) {
            TRACE_ERROR(
                "Unable to set in signal level limiting in the decoder\n");
          }
          for (int i = 0; i < static_cast<int>(sample_sizes.size()); i++) {
            std::istream& is_ok =
                m4a_file.read(&read_buffer[0], sample_sizes[i]);
            read_bytes = static_cast<int>(is_ok.gcount());
            if (read_bytes <= 0) {
              break;
            }
            valid = sample_sizes[i];
            err = aacDecoder_Fill(handle, &data, &valid, &valid);
            if (err != AAC_DEC_OK) {
              TRACE_ERROR(Strings::format("Fill failed: %x\n", err).c_str());
              break;
            }

            err = aacDecoder_DecodeFrame(handle, decode_buf, output_size, 0);
            if (err != AAC_DEC_OK) {
              TRACE_WARNING(
                  Strings::format("Decode failed: %x\n", err).c_str());
              continue;
            }
            cstream_info = aacDecoder_GetStreamInfo(handle);
            if (!cstream_info || cstream_info->sampleRate <= 0) {
              TRACE_ERROR("No stream info\n");
              break;
            }

            frame_length = cstream_info->frameSize * cstream_info->numChannels;
            for (int j = 0; j < frame_length; j++) {
              uint8_t* out = &output_buf[2 * j];
              out[0] = decode_buf[j] & 0xff;
              out[1] = decode_buf[j] >> 8;
            }
#ifdef _WIN32
            writeaudio(reinterpret_cast<LPSTR>(&output_buf[0]),
                       2 * frame_length);
#endif
          }
        }
#ifdef _WIN32
        freeblocks();
#endif
        close();
      } else {
        TRACE_ERROR("Neither mp4a or alac");
        System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
      }
    } else {
      TRACE_ERROR("Failed to open File");
      System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
    }
  }
};

class AACPlayer : public SimplePlayer {
 public:
  void play(const fs::path& path) {
#ifdef _WIN32
    FILE* in = _wfopen(path.c_str(), L"rb");
#else
    FILE* in = fopen(path.c_str(), "rb");
#endif

    if (!in) {
      TRACE_ERROR("failed to create input stream from file\n");
      return;
    }
    int output_size;
    uint8_t* output_buf;
    int16_t* decode_buf;
    UINT valid, packet_size;
    HANDLE_AACDECODER handle;
    AAC_DECODER_ERROR err;
    int frame_length = 0;

    handle = aacDecoder_Open(TT_MP4_ADTS, 1);

    output_size = 8 * 2 * 1024;
    output_buf = (uint8_t*)malloc(output_size);
    decode_buf = (int16_t*)malloc(output_size);
    bool updated = false;

    while (1) {
      uint8_t packet[10240], *ptr = packet;
      int n, i;
      n = fread(packet, 1, 7, in);
      if (n != 7)
        break;
      if (packet[0] != 0xff || (packet[1] & 0xf0) != 0xf0) {
        TRACE_ERROR("Not an ADTS packet\n");
        break;
      }
      packet_size =
          ((packet[3] & 0x03) << 11) | (packet[4] << 3) | (packet[5] >> 5);
      n = fread(packet + 7, 1, packet_size - 7, in);
      if (n != static_cast<int>(packet_size - 7)) {
        TRACE_ERROR("Partial packet\n");
        break;
      }
      valid = packet_size;
      err = aacDecoder_Fill(handle, &ptr, &packet_size, &valid);
      if (err != AAC_DEC_OK) {
        TRACE_ERROR(Strings::format("Fill failed: %x\n", err).c_str());
        break;
      }
      err = aacDecoder_DecodeFrame(handle, decode_buf, output_size, 0);
      if (err == AAC_DEC_NOT_ENOUGH_BITS)
        continue;
      if (err != AAC_DEC_OK) {
        TRACE_ERROR(Strings::format("Decode failed: %x\n", err).c_str());
        continue;
      }

      CStreamInfo* info = aacDecoder_GetStreamInfo(handle);
      if (!info || info->sampleRate <= 0) {
        TRACE_ERROR("No stream info\n");
        break;
      }
      if (!updated && info) {
        format.bits_per_sample = 16;
        format.channels = info->numChannels;
        format.sample_rate = info->sampleRate;
#ifdef _WIN32
        SimplePlayer::set_format();
#endif

#ifdef _WIN32
        setupblocks();
#endif
        open();

        updated = true;
      }
      if (updated) {
        frame_length = info->frameSize * info->numChannels;

        for (i = 0; i < frame_length; i++) {
          uint8_t* out = &output_buf[2 * i];
          out[0] = decode_buf[i] & 0xff;
          out[1] = decode_buf[i] >> 8;
        }
#ifdef _WIN32
        writeaudio(reinterpret_cast<LPSTR>(&output_buf[0]), 2 * frame_length);
#endif
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    free(output_buf);
    free(decode_buf);
    fclose(in);
    System::Process::exit_process(static_cast<int>(AudioStatus::kIoError));
  }
};

typedef std::map<std::string, std::unique_ptr<SimplePlayer>> PlayerRegistry;

PlayerRegistry get_default_registry() {
  using looper::System::Files::Extensions;
  PlayerRegistry registry;
  registry[Extensions::OPUS] = std::make_unique<looper::OpusPlayer>();
  registry[Extensions::MP3] = std::make_unique<looper::MP3Player>();
  registry[Extensions::OGG] = std::make_unique<looper::VorbisPlayer>();
  registry[Extensions::FLAC] = std::make_unique<looper::FlacPlayer>();
  registry[Extensions::OGA] = std::make_unique<looper::FlacPlayer>();
  registry[Extensions::WAV] = std::make_unique<looper::WavPlayer>();
  registry[Extensions::AIF] = std::make_unique<looper::AIFFPlayer>();
  registry[Extensions::AIFF] = std::make_unique<looper::AIFFPlayer>();
  registry[Extensions::CAF] = std::make_unique<looper::CAFPlayer>();
  registry[Extensions::ALAC] = std::make_unique<looper::CAFPlayer>();
  registry[Extensions::CAFF] = std::make_unique<looper::CAFPlayer>();
  registry[Extensions::M4A] = std::make_unique<looper::M4APlayer>();
  registry[Extensions::AAC] = std::make_unique<looper::AACPlayer>();
  return registry;
}

}  // namespace looper

#ifdef _WIN32
int __cdecl main()
#else
int main(int argc, char* argv[])
#endif
{
  using looper::Strings::equals_ignore_case;
  using looper::Strings::format;
  using looper::Strings::to_lower_case;
  using looper::Strings::to_wstring;
  using looper::System::Console::get_commandline_flags;
  using looper::System::Console::show_usage;
  using looper::System::Console::show_version;
  using looper::System::Files::is_required_file;
  using looper::System::Files::list_files;
  using looper::System::Files::list_files_recursive;
  using looper::System::Options::is_directory_option;
  using looper::System::Options::is_files_option;
  using looper::System::Options::is_help_option;
  using looper::System::Options::is_recursive_option;
  using looper::System::Options::is_version_option;

#ifdef _WIN32
  std::vector<std::string> flags = get_commandline_flags();
#else
  std::vector<std::string> flags = get_commandline_flags(argc, argv);
#endif

  std::string flag = flags[1];
  std::vector<fs::path> paths;

  if (is_version_option(flag)) {
    show_version();
  } else if (is_help_option(flag) || flags.size() < 3) {
    show_usage();
  } else if (is_directory_option(flag)) {
#ifdef _WIN32
    fs::path directory = to_wstring(flags[2].c_str());
#else
    fs::path directory = flags[2];
#endif
    paths = list_files(directory, is_required_file);
  } else if (is_recursive_option(flag)) {
#ifdef _WIN32
    fs::path directory = to_wstring(flags[2].c_str());
#else
    fs::path directory = flags[2];
#endif
    paths = list_files_recursive(directory, is_required_file);
  } else if (is_files_option(flag)) {
    for (size_t i = 2; i < flags.size(); ++i) {
#ifdef _WIN32
      fs::path path = to_wstring(flags[i].c_str());
#else
      fs::path path = flags[i];
#endif
      if (fs::exists(path) && is_required_file(path)) {
        paths.push_back(path);
      }
    }
  } else {
    TRACE_ERROR(format("option %s not found", flags[1].c_str()).c_str());
    show_usage();
  }
  looper::PlayerRegistry registry = looper::get_default_registry();

  bool should_continue = true;
  std::string extension;
  while (should_continue && paths.size() > 0) {
    for (auto& path : paths) {
      extension = to_lower_case(path.extension().string());
      if (fs::exists(path)) {
        if (registry.find(extension) != registry.end()) {
          registry[extension]->play(path);
        } else {
          should_continue = false;
          TRACE_ERROR("Wrong format cannot continue");
          break;
        }
      }
    }
  }

  return 0;
}
