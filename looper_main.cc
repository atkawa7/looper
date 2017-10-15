#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <FLAC/all.h>
#include <mpg123.h>
#include <opus/opusfile.h>
#include <vorbis/vorbisfile.h>

#ifdef _WIN32
#include <windows.h>
// Empty line to prevent clang-format moving it up
#include <shellapi.h>
static void CALLBACK waveOutProc(HWAVEOUT, UINT, DWORD, DWORD, DWORD);
#elif __linux__
#include <alsa/asoundlib.h>
#define PCM_DEVICE "default"
#endif

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

enum class LogLevel : int { ERR, WARNING, INFO, SUCCESS };

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
#elif __linux__
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
#else
#error Only Linux and Win32 builds are supported
#endif

#if __linux__
#define APP_STRNCASECMP strcasecmp
#elif _WIN32
#define APP_STRNCASECMP _stricmp
#else
#error "Not supported"
#endif

void print_color(std::string message, const Color color = Color::light_green) {
#if _WIN32
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

#define MAXBUFFERSIZE 0x400

std::string string_format(const char* format, ...) {
  char buffer[MAXBUFFERSIZE];
  va_list args;
  va_start(args, format);
  int sz = vsnprintf(buffer, MAXBUFFERSIZE, format, args);
  va_end(args);
  int copy_len = (std::min)(sz, MAXBUFFERSIZE);
  std::string output(buffer, copy_len);
  return output;
}

std::wstring wstring_format(const wchar_t* format, ...) {
  wchar_t buffer[MAXBUFFERSIZE];
  va_list args;
  va_start(args, format);
  int sz = vswprintf(buffer, MAXBUFFERSIZE, format, args);
  va_end(args);
  int copy_len = (std::min)(sz, MAXBUFFERSIZE);
  std::wstring output(buffer, copy_len);
  return output;
}

std::wstring to_wstring(const char* str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  return converter.from_bytes(str);
}

std::string to_string(const wchar_t* wstr) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

  return converter.to_bytes(wstr);
}

template <typename CharType>
std::vector<std::basic_string<CharType>> split(
    const std::basic_string<CharType>& text,
    CharType delim) {
  std::vector<std::basic_string<CharType>> tokens;
  if (delim == '\0') {
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

class TraceMessage {
 public:
  static void log(const char* function_name,
                  const char* log_,
                  const char* filename,
                  const int linenumber,
                  LogLevel level) {
    switch (level) {
      case LogLevel::ERR: {
        std::string log_info =
            string_format("ERROR: %s (%s) [%s:%d]\n", function_name, log_,
                          filename, linenumber);
        print_error(log_info);
      } break;
      case LogLevel::INFO: {
        std::string log_info =
            string_format("INFO: %s (%s) [%s:%d]\n", function_name, log_,
                          filename, linenumber);
        std::cout << log_info;
      } break;
      case LogLevel::WARNING: {
        std::string log_info =
            string_format("WARNING: %s (%s) [%s:%d]\n", function_name, log_,
                          filename, linenumber);
        print_color(log_info, Color::yellow);
      } break;
      case LogLevel::SUCCESS: {
        std::string log_info =
            string_format("SUCCESS: %s (%s) [%s:%d]\n", function_name, log_,
                          filename, linenumber);
        print_color(log_info);
      } break;
    }
  }
};

#define TRACE_INFO(info) \
  TraceMessage::log(__FUNCTION__, info, __FILE__, __LINE__, LogLevel::INFO)
#define TRACE_ERROR(error) \
  TraceMessage::log(__FUNCTION__, error, __FILE__, __LINE__, LogLevel::ERR)
#define TRACE_WARNING(warning)                                 \
  TraceMessage::log(__FUNCTION__, warning, __FILE__, __LINE__, \
                    LogLevel::WARNING)
#define TRACE_SUCCESS(success)                                 \
  TraceMessage::log(__FUNCTION__, success, __FILE__, __LINE__, \
                    LogLevel::SUCCESS)

enum class AudioStatus : int {
  kSuccess = 0,
  kIoError = 1,
  kAudioDeviceError = 2,
  kUknownError = 3
};

void AudioExitProcess(AudioStatus status) {
#ifdef __linux__
  ::_Exit(static_cast<int>(status));
#elif _WIN32
  ::ExitProcess(static_cast<int>(status));
#endif
}

bool IsLittleEndian() {
  int num = 1;
  return (*(char*)&num == 1);
}

typedef struct _AudioFormat {
  int channels, encoding, sample_rate, bits_per_sample;
  bool big_endian;
  _AudioFormat() { big_endian = !IsLittleEndian(); }

} AudioFormat;
typedef struct _WaveHeader {
  uint32_t ChunkID;
  uint32_t ChunkSize;
  uint32_t Format;
  uint32_t Subchunk1ID;
  uint32_t Subchunk1Size;
  uint16_t AudioFormat;
  uint16_t NumChannels;
  uint32_t SampleRate;
  uint32_t ByteRate;
  uint16_t BlockAlign;
  uint16_t BitsPerSample;
  uint32_t Subchunk2ID;
  uint32_t Subchunk2Size;
} WaveHeader;

typedef int AudioResult;

AudioFormat Format_From_WaveHeader(const WaveHeader& header) {
  AudioFormat fmt;
  fmt.bits_per_sample = header.BitsPerSample;
  fmt.channels = header.NumChannels;
  fmt.sample_rate = header.SampleRate;
  return fmt;
}

#ifdef _WIN32

// based on
// https://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=4422&lngWId=3
// based on
// https://chromium.googlesource.com/chromium/src.git/+/master/media/audio/win/waveout_output_win.cc

class SimplePlayer {
 public:
  explicit SimplePlayer(int block_count = default_block_count,
                        int block_size = default_block_size)
      : block_size(block_size), block_count(block_count) {
    free_blocks_count = block_count;

    InitializeCriticalSection(&waveCriticalSection);
  }

  void SetupBlocks() {
    current_block = 0;
    blocks = std::make_unique<unsigned char[]>(block_count * GetBlockSize());
    for (int ix = 0; ix < block_count; ++ix) {
      WAVEHDR* block = GetBlock(ix);
      block->lpData = reinterpret_cast<char*>(block) + sizeof(WAVEHDR);
      block->dwBufferLength = block_size;
      block->dwBytesRecorded = 0;
      block->dwFlags = WHDR_DONE;
      block->dwLoops = 0;
    }
  }

  void SetFormat(const AudioFormat& fmt) {
    wfx.nSamplesPerSec = fmt.sample_rate;
    wfx.wBitsPerSample = static_cast<WORD>(fmt.bits_per_sample);
    wfx.nChannels = static_cast<WORD>(fmt.channels);
    wfx.cbSize = 0;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nBlockAlign = (wfx.wBitsPerSample * wfx.nChannels) >> 3;
    wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;
  }

  void Open() {
    if (::waveOutOpen(&hWaveOut, WAVE_MAPPER,
                      reinterpret_cast<LPCWAVEFORMATEX>(&wfx),
                      reinterpret_cast<DWORD_PTR>(waveOutProc),
                      reinterpret_cast<DWORD_PTR>(this),
                      CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
      TRACE_INFO("unable to open wave mapper device");
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    } else {
      TRACE_INFO("Opening Device");
    }
    // if (::waveOutSetVolume(hWaveOut, 0xFFFFFFFF) != MMSYSERR_NOERROR)
    // {
    //         TRACE_INFO("Failed to Set Device");
    // }
  }

  int BitsPerSample() { return wfx.wBitsPerSample; }

  int Channels() { return wfx.nChannels; }

  void IncrementBlock() {
    EnterCriticalSection(&waveCriticalSection);
    free_blocks_count++;
    LeaveCriticalSection(&waveCriticalSection);
  }

  void DecrementBlock() {
    EnterCriticalSection(&waveCriticalSection);
    free_blocks_count--;
    LeaveCriticalSection(&waveCriticalSection);
  }
  void FreeBlocks() {
    while (free_blocks_count < block_count)
      Sleep(10);
    for (int ix = 0; ix < block_count; ++ix) {
      ::waveOutUnprepareHeader(hWaveOut, GetBlock(ix), sizeof(WAVEHDR));
    }
    blocks.reset();
  }

  void WriteAudio(LPSTR data, int size) {
    WAVEHDR* current;
    int remain;

    current = GetBlock(current_block);

    while (size > 0) {
      if (current->dwFlags & WHDR_PREPARED)
        waveOutUnprepareHeader(hWaveOut, current, sizeof(WAVEHDR));

      if (size < (int)(block_size - current->dwUser)) {
        memcpy(current->lpData + current->dwUser, data, size);
        current->dwUser += size;
        break;
      }

      remain = block_size - current->dwUser;
      memcpy(current->lpData + current->dwUser, data, remain);
      size -= remain;
      data += remain;
      current->dwBufferLength = block_size;

      waveOutPrepareHeader(hWaveOut, current, sizeof(WAVEHDR));
      waveOutWrite(hWaveOut, current, sizeof(WAVEHDR));

      DecrementBlock();

      while (!free_blocks_count)
        Sleep(10);

      current_block++;
      current_block %= block_count;

      current = GetBlock(current_block);
      current->dwUser = 0;
    }
  }

  ~SimplePlayer() { DeleteCriticalSection(&waveCriticalSection); }
  void Close() {
    ::waveOutClose(hWaveOut);
    TRACE_INFO("closing device");
  }

  enum {
    default_buffer_size = 0x400,
    default_block_count = 0x20,
    default_block_size = 0x2000
  };

 private:
  int GetBlockSize() { return (sizeof(WAVEHDR) + block_size + 15u) & (~15u); }

  WAVEHDR* GetBlock(int position) {
    return reinterpret_cast<WAVEHDR*>(&blocks[GetBlockSize() * position]);
  }
  std::unique_ptr<unsigned char[]> blocks;
  WAVEFORMATEX wfx;
  HWAVEOUT hWaveOut;
  CRITICAL_SECTION waveCriticalSection;
  volatile int free_blocks_count;
  int block_size, block_count, current_block;
};

#elif __linux__
class SimplePlayer {
 public:
  snd_pcm_format_t get_pcm_format() {
    switch (bits_per_sample) {
      case 64:
        return ((IsLittleEndian()) ? SND_PCM_FORMAT_FLOAT64_LE
                                   : SND_PCM_FORMAT_FLOAT64_BE);
      case 32:
        return ((IsLittleEndian()) ? SND_PCM_FORMAT_S32_LE
                                   : SND_PCM_FORMAT_S32_BE);
      case 24:
        return ((IsLittleEndian()) ? SND_PCM_FORMAT_S24_LE
                                   : SND_PCM_FORMAT_S24_BE);
      case 16:
        return ((IsLittleEndian()) ? SND_PCM_FORMAT_S16_LE
                                   : SND_PCM_FORMAT_S16_BE);
      case 8:
        return SND_PCM_FORMAT_S8;
      default:
        return SND_PCM_FORMAT_UNKNOWN;
    }
  }
  void SetFormat(const AudioFormat& format) {
    channels = format.channels;
    bits_per_sample = format.bits_per_sample;
    encoding = format.encoding;
    sample_rate = format.sample_rate;
  }

  int BitsPerSample() { return bits_per_sample; }
  int Channels() { return channels; }
  void Open() {
    AudioResult result;
    if ((result = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK,
                               0)) < 0) {
      std::string message = string_format("Can't open \"%s\" PCM device. %s",
                                          PCM_DEVICE, snd_strerror(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    }

    snd_pcm_hw_params_alloca(&params);

    snd_pcm_hw_params_any(pcm_handle, params);

    if ((result = snd_pcm_hw_params_set_access(
             pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
      std::string message =
          string_format("Can't set interleaved mode. %s", snd_strerror(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    }

    if ((result = snd_pcm_hw_params_set_format(pcm_handle, params,
                                               get_pcm_format())) < 0) {
      std::string message =
          string_format("Can't set format. %s", snd_strerror(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    }
    if ((result = snd_pcm_hw_params_set_channels(pcm_handle, params,
                                                 channels)) < 0) {
      std::string message =
          string_format("Can't set channels number. %s", snd_strerror(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    }

    if (((result = snd_pcm_hw_params_set_rate_near(
              pcm_handle, params, reinterpret_cast<unsigned int*>(&sample_rate),
              0))) < 0) {
      std::string message =
          string_format("Can't set sample_rate. %s", snd_strerror(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    }

    if ((result = snd_pcm_hw_params(pcm_handle, params)) < 0) {
      std::string message = string_format("Can't set harware parameters. %s",
                                          snd_strerror(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kAudioDeviceError);
    }
  }
  void Close() {
    if (pcm_handle) {
      snd_pcm_drain(pcm_handle);
      snd_pcm_close(pcm_handle);
    }
  }
  snd_pcm_uframes_t bytes_to_frames(ssize_t _bytes) {
    return snd_pcm_bytes_to_frames(pcm_handle, _bytes);
  }
  void WriteAudio(const void* buffer, snd_pcm_uframes_t _frames) {
    AudioResult result;
    if ((result = snd_pcm_writei(pcm_handle, buffer, _frames)) == -EPIPE) {
      snd_pcm_prepare(pcm_handle);
    } else if (result < 0) {
      std::string message =
          string_format("Can't write to PCM device. %s", snd_strerror(result));
      TRACE_ERROR(message.c_str());
    }
  }

  snd_pcm_t* pcm_handle;
  snd_pcm_hw_params_t* params;
  snd_pcm_uframes_t frames;
  int channels, encoding, sample_rate, bits_per_sample;
  enum { default_buffer_size = 0x400 };
};
#endif

#ifdef _WIN32
static void CALLBACK waveOutProc(HWAVEOUT hWaveOut,
                                 UINT uMsg,
                                 DWORD dwInstance,
                                 DWORD dwParam1,
                                 DWORD dwParam2) {
  (void)hWaveOut;
  (void)dwParam1;
  (void)dwParam2;

  SimplePlayer* player = reinterpret_cast<SimplePlayer*>(dwInstance);
  if (uMsg != WOM_DONE)
    return;
  player->IncrementBlock();
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

void PrintPlayingInfo(const Metadata& meta) {
  print_color("Playing Info\n", Color::light_yellow);
  print_color(to_string(meta));
  print_color("Starting to play\n", Color::light_yellow);
}

class WavPlayer : public SimplePlayer {
 public:
  void play(const std::string& path) {
#ifdef _WIN32
    std::ifstream wave_file(to_wstring(path.c_str()), std::ifstream::binary);
#else
    std::ifstream wave_file(path, std::ifstream::binary);
#endif
    if (wave_file) {
      std::string message = string_format("Opened %s", path.c_str());
      TRACE_INFO(message.c_str());
      WaveHeader header;
      int read_bytes = 0;
      std::istream& header_is_ok =
          wave_file.read(reinterpret_cast<char*>(&header), sizeof(WaveHeader));
      read_bytes = static_cast<int>(header_is_ok.gcount());

      if (read_bytes < sizeof(WaveHeader)) {
        TRACE_ERROR("Small header size");
        AudioExitProcess(AudioStatus::kIoError);
      }

      SetFormat(Format_From_WaveHeader(header));

      Metadata mt;
      fs::path current_path(path);
      mt.title = current_path.stem().string();
      PrintPlayingInfo(mt);

#ifdef _WIN32
      SetupBlocks();
#endif

      Open();

      std::string buffer(default_buffer_size, '\0');
      for (;;) {
        std::istream& is_ok = wave_file.read(&buffer[0], default_buffer_size);
        read_bytes = static_cast<int>(is_ok.gcount());

        if (read_bytes <= 0)
          break;
#ifdef _WIN32
        WriteAudio(&buffer[0], static_cast<int>(is_ok.gcount()));
#elif __linux__
        frames = bytes_to_frames(default_buffer_size);
        WriteAudio(&buffer[0], frames);
#endif
      }
#ifdef _WIN32
      FreeBlocks();
#endif
      Close();
      print_color("Done Playing Song\n\n", Color::light_yellow);
    } else {
      TRACE_ERROR("Failed to open file");
      AudioExitProcess(AudioStatus::kIoError);
    }
  }
};

typedef mpg123_handle MPG123Handle;

AudioFormat Format_From_MPG123Handle(MPG123Handle* mh) {
  AudioFormat fmt;

  mpg123_getformat(mh, reinterpret_cast<long int*>(&fmt.sample_rate),
                   &fmt.channels, &fmt.encoding);

  if (fmt.encoding & MPG123_ENC_FLOAT_64)
    fmt.bits_per_sample = 64;
  else if (fmt.encoding & MPG123_ENC_FLOAT_32)
    fmt.bits_per_sample = 32;
  else if (fmt.encoding & MPG123_ENC_16)
    fmt.bits_per_sample = 16;
  else
    fmt.bits_per_sample = 8;
  return fmt;
}

Metadata Metadata_From_Handle(MPG123Handle* mh) {
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
  void play(const std::string& path) {
    MPG123Handle* mh;

    size_t buffer_size, read_bytes;

    AudioResult result = MPG123_OK;

    result = mpg123_init();

    if (result != MPG123_OK) {
      std::string message =
          string_format("mpg123_init error: %s", ErrorCodeToString(result));
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kIoError);
    }

    mh = mpg123_new(nullptr, &result);

    if (result != MPG123_OK) {
      std::string message =
          string_format("mpg123_new error: %s", ErrorCodeToString(result));
      TRACE_ERROR(message.c_str());
      Cleanup(mh);
      AudioExitProcess(AudioStatus::kIoError);
    }

    result = mpg123_open(mh, path.c_str());

    if (result != MPG123_OK) {
      std::string message =
          string_format("Cannot open file: %s", HandleErrorToString(mh));
      TRACE_ERROR(message.c_str());
      Cleanup(mh);
      AudioExitProcess(AudioStatus::kIoError);
    } else {
      std::string message = string_format("Opened %s", path.c_str());
      TRACE_INFO(message.c_str());
    }

    PrintPlayingInfo(Metadata_From_Handle(mh));

    buffer_size = mpg123_outblock(mh);
    std::string buffer(buffer_size, '\0');
    SetFormat(Format_From_MPG123Handle(mh));

#ifdef _WIN32
    SetupBlocks();
#endif

    Open();

    while (mpg123_read(mh, reinterpret_cast<unsigned char*>(&buffer[0]),
                       buffer_size, &read_bytes) == MPG123_OK) {
      if (read_bytes <= 0)
        break;
#ifdef _WIN32
      WriteAudio(&buffer[0], read_bytes);
#elif __linux__
      frames = bytes_to_frames(read_bytes);
      WriteAudio(&buffer[0], frames);
#endif
    }

#ifdef _WIN32
    FreeBlocks();
#endif

    Cleanup(mh);
    Close();
    print_color("Done Playing Song\n\n", Color::light_yellow);
  }

  const char* ErrorCodeToString(int error_code) {
    return mpg123_plain_strerror(error_code);
  }
  const char* HandleErrorToString(MPG123Handle* mh) {
    return mpg123_strerror(mh);
  }

  void Cleanup(MPG123Handle* mh) {
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
  }
};

typedef vorbis_info VorbisInfo;
typedef vorbis_comment VorbisComment;

AudioFormat Format_From_VorbisFile(OggVorbis_File* vf) {
  AudioFormat fmt;
  VorbisInfo* vi = ov_info(vf, -1);
  if (vi != nullptr) {
    fmt.channels = vi->channels;
    fmt.sample_rate = vi->rate;
    fmt.bits_per_sample = 16;
  }
  return fmt;
};

void MetaAppendField(Metadata* meta, std::string& key, std::string& value) {
  if (APP_STRNCASECMP(key.c_str(), "artist") == 0) {
    meta->artist.append(value);
  } else if (APP_STRNCASECMP(key.c_str(), "title") == 0) {
    meta->title.append(value);
  } else if (APP_STRNCASECMP(key.c_str(), "year") == 0) {
    meta->year.append(value);
  } else if (APP_STRNCASECMP(key.c_str(), "date") == 0) {
    meta->year.append(value);
  } else if (APP_STRNCASECMP(key.c_str(), "genre") == 0) {
    meta->genre.append(value);
  } else if (APP_STRNCASECMP(key.c_str(), "album") == 0) {
    meta->album.append(value);
  } else if (APP_STRNCASECMP(key.c_str(), "comment") == 0) {
    meta->comment.append(value);
  }
};

Metadata Metadata_From_OggVorbis_File(OggVorbis_File* vf) {
  Metadata mt;
  const VorbisComment* comment = ov_comment(vf, -1);
  if (comment != nullptr) {
    for (int i = 0; i < comment->comments; i++) {
      size_t comment_length = comment->comment_lengths[i];
      std::string comment_str(comment_length + 1, '\0');
      strncpy(&comment_str[0], comment->user_comments[i], comment_length);
      std::vector<std::string> tokens = split(comment_str, '=');
      if (tokens.size() > 1) {
        for (size_t j = 1; j < tokens.size(); j++) {
          MetaAppendField(&mt, tokens[0], tokens[j]);
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
  void play(const std::string& path) {
    OggVorbis_File vf;
    size_t buffer_size = default_buffer_size, read_bytes = 0;

    AudioResult result, secs;

#ifdef _WIN32
    std::wstring wpath = to_wstring(path.c_str());
    result = ov_wfopen(wpath.c_str(), &vf);
#else
    result = ov_fopen(path.c_str(), &vf);
#endif

    if (result != 0) {
      std::string message = string_format("Error opening file %d", result);
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kIoError);
    } else {
      std::string message = string_format("Opened %s", path.c_str());
      TRACE_INFO(message.c_str());
    }

    std::string buffer(buffer_size, '\0');

    SetFormat(Format_From_VorbisFile(&vf));

    PrintPlayingInfo(Metadata_From_OggVorbis_File(&vf));

#ifdef _WIN32
    SetupBlocks();
#endif

    Open();
    int is_bigendian = (IsLittleEndian()) ? 0 : 1;
    int word_size = (BitsPerSample() == 8) ? 1 : 2;

    for (;;) {
      read_bytes = ov_read(&vf, &buffer[0], buffer_size, is_bigendian,
                           word_size, 1, &secs);

      if (read_bytes <= 0)
        break;
#ifdef _WIN32
      WriteAudio(&buffer[0], read_bytes);
#elif __linux__
      frames = bytes_to_frames(read_bytes);
      WriteAudio(&buffer[0], frames);
#endif
    }

#ifdef _WIN32
    FreeBlocks();
#endif
    Cleanup(&vf);
    Close();
    print_color("Done Playing Song\n\n", Color::light_yellow);
  }

  void Cleanup(OggVorbis_File* vf) { ov_clear(vf); }
};

AudioFormat Format_From_FLAC_Metadata(const FLAC__StreamMetadata* metadata) {
  AudioFormat fmt;
  fmt.sample_rate = metadata->data.stream_info.sample_rate;
  fmt.channels = metadata->data.stream_info.channels;

#ifdef _WIN32
  int bits_per_sample = metadata->data.stream_info.bits_per_sample;
  fmt.bits_per_sample = (bits_per_sample == 24) ? 32 : bits_per_sample;
#else
  fmt.bits_per_sample = metadata->data.stream_info.bits_per_sample;
#endif

  return fmt;
}

Metadata Metadata_FLAC__StreamMetadata(const FLAC__StreamMetadata* metadata) {
  Metadata mt;
  auto tags = metadata->data.vorbis_comment;
  for (size_t i = 0; i < tags.num_comments; i++) {
    auto flac_comment = tags.comments[i];
    size_t length = sizeof(FLAC__byte) * flac_comment.length;
    std::string comment(length + 1, '\0');
    strncpy(&comment[0], reinterpret_cast<char*>(flac_comment.entry), length);
    std::vector<std::string> tokens = split(comment, '=');
    if (tokens.size() > 1) {
      for (size_t j = 1; j < tokens.size(); j++) {
        MetaAppendField(&mt, tokens[0], tokens[j]);
      }
    }
  }
  return mt;
}

class FlacPlayer : public SimplePlayer {
 public:
  void play(const std::string& path) {
    FLAC__bool ok = true;
    FLAC__StreamDecoder* decoder = 0;
    FLAC__StreamDecoderInitStatus init_status;

#if _WIN32
    SetupBlocks();
#endif
    if ((decoder = FLAC__stream_decoder_new()) == nullptr) {
      TRACE_ERROR("allocating decoder");
      AudioExitProcess(AudioStatus::kIoError);
    }

    FLAC__stream_decoder_set_md5_checking(decoder, true);
    FLAC__stream_decoder_set_metadata_respond(decoder,
                                              FLAC__METADATA_TYPE_STREAMINFO);
    FLAC__stream_decoder_set_metadata_respond(
        decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);

#ifdef _WIN32

    std::wstring wpath = to_wstring(path.c_str());
    FILE* audio_file = _wfopen(wpath.c_str(), L"rb");
    if (audio_file == nullptr) {
      TRACE_ERROR("Failed to open file");
      AudioExitProcess(AudioStatus::kIoError);
    }
    init_status =
        FLAC__stream_decoder_init_FILE(decoder, audio_file, write_callback,
                                       metadata_callback, error_callback, this);
#else
    init_status =
        FLAC__stream_decoder_init_file(decoder, path.c_str(), write_callback,
                                       metadata_callback, error_callback, this);
#endif

    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
      std::string message = string_format(
          "initializing decoder: %s  %s",
          FLAC__StreamDecoderInitStatusString[init_status], path.c_str());
      TRACE_ERROR(message.c_str());
      AudioExitProcess(AudioStatus::kIoError);
      ok = false;
    } else {
      std::string message = string_format("Opened %s", path.c_str());
      TRACE_INFO(message.c_str());
    }

    if (ok) {
      ok = FLAC__stream_decoder_process_until_end_of_stream(decoder);
      std::string message = string_format(
          "decoding: %s   state: %s", ok ? "succeeded" : "FAILED",
          FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(
              decoder)]);

      if (ok) {
        TRACE_SUCCESS(message.c_str());
      } else {
        TRACE_ERROR(message.c_str());
      }
    }

#ifdef _WIN32
    FreeBlocks();
    fclose(audio_file);
#endif
    Cleanup(decoder);
    Close();
    print_color("Done Playing Song\n\n", Color::light_yellow);
  }

  void Cleanup(FLAC__StreamDecoder* decoder) {
    FLAC__stream_decoder_delete(decoder);
  }

  static FLAC__StreamDecoderWriteStatus write_callback(
      const FLAC__StreamDecoder* decoder,
      const FLAC__Frame* frame,
      const FLAC__int32* const buffer[],
      void* client_data) {
    (void)decoder;

    FlacPlayer* player = reinterpret_cast<FlacPlayer*>(client_data);
    uint32_t samples = frame->header.blocksize,
             channels = frame->header.channels;

    int bits_per_sample = player->BitsPerSample();

    static int32_t
        buf[FLAC__MAX_BLOCK_SIZE * FLAC__MAX_CHANNELS * sizeof(uint32_t)];

#ifdef _WIN32
    uint32_t decoded_size = samples * channels * (bits_per_sample / 8);
    uint16_t* u16buf = reinterpret_cast<uint16_t*>(buf);
    uint32_t* u32buf = reinterpret_cast<uint32_t*>(buf);
    uint8_t* u8buf = reinterpret_cast<uint8_t*>(buf);
#endif

    if (!(channels == 2 || channels == 1)) {
      std::string message = string_format(
          "This frame contains %d channels (should be 1 or 2)", channels);
      TRACE_ERROR(message.c_str());
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (buffer[0] == nullptr) {
      TRACE_ERROR("buffer[0] is null");
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (buffer[1] == nullptr && 2 == channels) {
      TRACE_ERROR("buffer[1] is null");
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if (bits_per_sample == 8 || bits_per_sample == 16 ||
        bits_per_sample == 24 || bits_per_sample == 32) {
      for (uint32_t sample = 0, i = 0; sample < samples; sample++) {
        for (uint32_t channel = 0; channel < channels; channel++, i++) {
#ifdef _WIN32
          switch (bits_per_sample) {
            case 8:
              u8buf[i] = static_cast<uint8_t>(buffer[channel][sample] & 0xff);
              break;
            case 16:
              u16buf[i] =
                  static_cast<uint16_t>(buffer[channel][sample] & 0xffffff);
              break;
            case 24:
              u32buf[i] = static_cast<uint32_t>(buffer[channel][sample]);
              break;
            case 32:
              u32buf[i] = static_cast<uint32_t>(buffer[channel][sample]);
              break;
          }
#elif __linux__
          buf[i] = static_cast<uint32_t>(buffer[channel][sample]);
#endif
        }
      }

#ifdef _WIN32
      player->WriteAudio(reinterpret_cast<LPSTR>(buf), decoded_size);
#elif __linux
      player->WriteAudio(buf, samples);
#endif
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
        player->SetFormat(Format_From_FLAC_Metadata(metadata));
        player->Open();
      } break;
      case FLAC__METADATA_TYPE_VORBIS_COMMENT: {
        PrintPlayingInfo(Metadata_FLAC__StreamMetadata(metadata));
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
    std::string message = string_format(
        "Got error callback: %s", FLAC__StreamDecoderErrorStatusString[status]);
    TRACE_ERROR(message.c_str());
  }
};

AudioFormat Format_From_OggOpusFile(OggOpusFile* op_file) {
  AudioFormat fmt;

  const OpusHead* head = op_head(op_file, -1);
  if (head != nullptr) {
    fmt.sample_rate = head->input_sample_rate;
    fmt.channels = head->channel_count;
    fmt.bits_per_sample = 16;
  }
  return fmt;
}
Metadata Metadata_From_OggOpusFile(OggOpusFile* op_file) {
  Metadata mt;
  const OpusTags* tags = op_tags(op_file, -1);
  if (tags != nullptr) {
    for (int i = 0; i < tags->comments; i++) {
      size_t comment_length = tags->comment_lengths[i];
      std::string comment(comment_length + 1, '\0');
      strncpy(&comment[0], tags->user_comments[i], comment_length);
      std::vector<std::string> tokens = split(comment, '=');
      if (tokens.size() > 1) {
        for (size_t j = 1; j < tokens.size(); j++) {
          MetaAppendField(&mt, tokens[0], tokens[j]);
        }
      }
    }
  }
  return mt;
}
class OpusPlayer : public SimplePlayer {
 public:
  void play(const std::string& path) {
    int err;
    OggOpusFile* op_file = op_open_file(path.c_str(), &err);
    if (err) {
      TRACE_ERROR("Failed to Open File");
      AudioExitProcess(AudioStatus::kIoError);
    } else {
      std::string message = string_format("Opened %s", path.c_str());
      TRACE_INFO(message.c_str());
    }

    SetFormat(Format_From_OggOpusFile(op_file));

    PrintPlayingInfo(Metadata_From_OggOpusFile(op_file));

    static opus_int16 buf[0x1000];

    int read_bytes;

#ifdef _WIN32
    SetupBlocks();
#endif
    Open();

    for (;;) {
      read_bytes = op_read(op_file, buf, 0x1000, nullptr);
      if (read_bytes <= 0) {
        break;
      }
#ifdef _WIN32
      WriteAudio(reinterpret_cast<LPSTR>(buf), read_bytes * Channels() * 2);
#elif __linux__
      frames = bytes_to_frames(read_bytes * Channels() * 2);
      WriteAudio(&buf[0], frames);
#endif
    }
#ifdef _WIN32
    FreeBlocks();
#endif
    Close();
    print_color("Done Playing Song\n\n", Color::light_yellow);
  }
};

typedef std::map<std::string, SimplePlayer*> PlayerRegistry;

#ifdef _WIN32
int __cdecl main()
#else
int main(int argc, char* argv[])
#endif
{

  WavPlayer wav_player;
  MP3Player mp3_player;
  VorbisPlayer vorbis_player;
  FlacPlayer flac_player;
  OpusPlayer opus_player;

  PlayerRegistry registry = {{".opus", &opus_player},
                             {".mp3", &mp3_player},
                             {".ogg", &vorbis_player},
                             {".flac", &flac_player},
                             {".wav", &wav_player}};

  std::vector<std::string> songs;
  std::string extension;

#ifdef _WIN32
  LPWSTR* szArgList;
  int nArgs;

  szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
  if (nullptr == szArgList || nArgs < 2) {
    TRACE_ERROR("commandline failed");
    AudioExitProcess(AudioStatus::kIoError);
  } else {
    for (int i = 1; i < nArgs; ++i) {
      songs.push_back(to_string(szArgList[i]));
    }
  }
  LocalFree(szArgList);
#elif __linux__
  if (argv == nullptr || argc < 2) {
    TRACE_ERROR("commandline failed");
    AudioExitProcess(AudioStatus::kIoError);
  } else {
    for (int i = 1; i < argc; ++i) {
      songs.push_back(argv[i]);
    }
  }
#endif
  fs::path current_path;
  bool should_continue = true;
  while (should_continue) {
    for (auto& song : songs) {
#ifdef _WIN32
      current_path = to_wstring(song.c_str());
#else
      current_path = song;
#endif
      extension = current_path.extension().string();
      if (fs::exists(current_path)) {
        if (registry.find(extension) != registry.end()) {
          if (extension == ".mp3") {
            MP3Player* player =
                reinterpret_cast<MP3Player*>(registry[extension]);
            player->play(song);
          } else if (extension == ".opus") {
            OpusPlayer* player =
                reinterpret_cast<OpusPlayer*>(registry[extension]);
            player->play(song);
          } else if (extension == ".wav") {
            WavPlayer* player =
                reinterpret_cast<WavPlayer*>(registry[extension]);
            player->play(song);
          } else if (extension == ".ogg") {
            VorbisPlayer* player =
                reinterpret_cast<VorbisPlayer*>(registry[extension]);
            player->play(song);
          } else if (extension == ".flac") {
            FlacPlayer* player =
                reinterpret_cast<FlacPlayer*>(registry[extension]);
            player->play(song);
          }
        } else {
          should_continue = false;
          TRACE_ERROR("Wrong format cannot continue");
          break;
        };
      }
    }
  }
  return 0;
}
