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

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif /* __APPLE__ */

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

#include <AudioToolbox/AudioToolbox.h>
#include <aacdecoder_lib.h>
#include <ogg/ogg.h>
#include <pthread.h>
#include <stdio.h>
#include <vorbis/vorbisfile.h>

namespace fs = std::filesystem;

#ifdef FALSE
#undef FALSE
#endif

#ifdef EOF
#undef EOF
#endif

enum class OV_Error {
  FALSE = -1,
  EOF = -2,
  HOLE = -3,
  READ = -128,
  FAULT = -129,
  IMPL = -130,
  INVAL = -131,
  NOTVORBIS = -132,
  BADHEADER = -133,
  VERSION = -134,
  NOTAUDIO = -135,
  BADPACKET = -136,
  BADLINK = -137,
  NOSEEK = -138
};

static std::unordered_map<OV_Error, std::string> OV_Errors ={
  { OV_Error::FALSE, "The call returned a 'false' status (eg, ov_bitrate_instant can return OV_FALSE if playback is not in progress, and thus there is no instantaneous bitrate information to report."},
  { OV_Error::EOF, "Reached end of file"},
  { OV_Error::HOLE ,"libvorbis/libvorbisfile is alerting the application that there was an interruption in the data (one of: garbage between pages, loss of sync followed by recapture, or a corrupt page"},
  { OV_Error::READ, "A read from media returned an error"},
  { OV_Error::FAULT, "Internal logic fault.\nThis is likely a bug or heap / stack corruption"},
  { OV_Error::IMPL , "The bitstream makes use of a feature not implemented in this library version."},
  { OV_Error::INVAL , "Invalid argument value."},
  { OV_Error::NOTVORBIS , "Bitstream is not Vorbis data"},
  { OV_Error::BADHEADER, "Invalid Vorbis bitstream header"},
  { OV_Error::VERSION , "Vorbis version mismatch"},
  { OV_Error::NOTAUDIO , "Packet data submitted to vorbis_synthesis is not audio data."},
  { OV_Error::BADPACKET, "Invalid packet submitted to vorbis_synthesis."},
  { OV_Error::BADLINK , "Invalid stream section supplied to libvorbis/libvorbisfile, or the requested link is corrupt."},
  { OV_Error::NOSEEK , "Bitstream is not seekable."}
  };

inline std::string to_string(const OV_Error& err) {
  return OV_Errors[err];
}

static void outputCallback(void* aqData,
                           AudioQueueRef inAQ,
                           AudioQueueBufferRef pcmOut);

int count = 0;

class Engine {
  static const int kMaxBufferSize = 0x50000;
  static const int kMaxNumberOfBuffers = 3;
  AudioQueueRef audio_queue_;
  // AudioQueueBufferRef audio_queue_Buffers[kMaxNumberOfBuffers];
  AudioStreamPacketDescription* mPacketDescs;
  bool mIsRunning;
  OggVorbis_File mOVFile;
  vorbis_info* mInfo;
  FILE* mOggFile;
  AudioStreamBasicDescription mDescription;
  // long mBytesRead;

 public:
  Engine() {}
  ~Engine() { this->close(); }

  bool is_running() { return mIsRunning; }

  bool has_stopped() { return !mIsRunning; }

  long read_chunk(char* buffer,
                  int length,
                  int bigendianp,
                  int word,
                  int sgned,
                  int* bitstream) {
    return ov_read(&mOVFile, buffer, length, bigendianp, word, sgned,
                   bitstream);
  }

  OSStatus stop() {
    this->mIsRunning = false;
    return AudioQueueStop(audio_queue_, true);
  }

  OSStatus async_stop() {
    this->mIsRunning = false;
    return AudioQueueStop(audio_queue_, false);
  }

  OSStatus enqueue_buffer(AudioQueueBufferRef audio_buffer) {
    return AudioQueueEnqueueBuffer(audio_queue_, audio_buffer, 0, 0);
  }

  static char* codeToString(UInt32 code) {
    static char str[5] = {'\0'};
    UInt32 swapped = CFSwapInt32HostToBig(code);
    memcpy(str, &swapped, sizeof(swapped));
    return str;
  }

  void print_error(OSStatus error) {
    printf("%s => %d", codeToString(error), error);
  }

  void close_files() {
    ov_clear(&mOVFile);
    if (mOggFile) {
      fclose(mOggFile);
      mOggFile = nullptr;
    }
  }

  void close() {
    stop();

    if (audio_queue_) {
      OSStatus err = AudioQueueDispose(audio_queue_, true);
      audio_queue_ = nullptr;
      if (err != noErr) {
        print_error(err);
      }
    }
    close_files();
  }

  void async_close() {
    async_stop();
    if (audio_queue_) {
      OSStatus err = AudioQueueDispose(audio_queue_, false);
      audio_queue_ = nullptr;
      if (err != noErr) {
        print_error(err);
      }
    }
    close_files();
  }

  void play(char* location) {
    mOggFile = fopen(location, "r");
    if (mOggFile == nullptr) {
      printf("Unable to open input file.\n");
      exit(1);
    }

    int err =
        ov_open_callbacks(mOggFile, &mOVFile, nullptr, 0, OV_CALLBACKS_NOCLOSE);
    if (err != 0) {
      printf("Unable to set callbacks.\n");
      exit(1);
    }

    mInfo = ov_info(&(mOVFile), -1);
    FillOutASBDForLPCM(mDescription, mInfo->rate, mInfo->channels, 16, 16,
                       false, false);
    this->prepareAudioBuffers();
    this->playAudio();
  }

  void prepareAudioBuffers() {
    OSStatus status = AudioQueueNewOutput(
        &(mDescription), outputCallback, this, CFRunLoopGetCurrent(),
        kCFRunLoopCommonModes, 0, &audio_queue_);

    if (status != noErr) {
      printf("Error AudioQueueNewOutput");
      exit(1);
    }

    mPacketDescs = nullptr;

    mIsRunning = true;

    for (int i = 0; i < kMaxNumberOfBuffers; i++) {
      AudioQueueBufferRef ref;
      status = AudioQueueAllocateBuffer(audio_queue_, kMaxBufferSize, &ref);
      if (status != noErr) {
        this->close();
        printf("Error allocating audio queue buffer %d\n", i);
        exit(1);
      }
      outputCallback(this, audio_queue_, ref);
    }
  }
  void playAudio() {
    // set gain and start queue

    std::cout << "Sequence \t" << ++count << std::endl;

    AudioQueueSetParameter(audio_queue_, kAudioQueueParam_Volume, 1.0);
    AudioQueueStart(audio_queue_, nullptr);

    // maintain run loop
    do {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
    } while (mIsRunning);

    // run a bit longer to fully flush audio buffers
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false);

    async_close();
  }
};

static void outputCallback(void* aqData,
                           AudioQueueRef inAQ,
                           AudioQueueBufferRef pcmOut) {
  Engine* player = static_cast<Engine*>(aqData);

  if (player->has_stopped())
    return;

  long bytes_read = 0;
  long total_bytes_read = 0;
  int current_section = -1;

  for (;;) {
    int buffer_size = pcmOut->mAudioDataBytesCapacity - total_bytes_read;
    bool full = buffer_size < 0;
    if (full) {
      break;
    }

    char* offset = (char*)pcmOut->mAudioData + total_bytes_read;
    bytes_read =
        player->read_chunk(offset, buffer_size, false, 2, 1, &current_section);
    if (bytes_read <= 0) {
      break;
    }

    total_bytes_read += bytes_read;
  }

  if (bytes_read <= 0) {
    if (bytes_read == static_cast<long>(OV_Error::BADLINK)) {
      printf("Corrupt Bitstream; exiting");
      exit(1);
    }

    pcmOut->mAudioDataByteSize = (UInt32)total_bytes_read;
    pcmOut->mPacketDescriptionCount = 0;
    OSStatus status = player->enqueue_buffer(pcmOut);

    if (status != noErr) {
      player->stop();
      printf("Error %d AudioQueueEnqueueBuffer", status);
      exit(1);
    }

    if (bytes_read == 0) {
      player->async_stop();
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage: vorplay <file>\n");
    exit(1);
  }

  Engine engine;
  int i = 0;
  while (i++ < 1) {
    engine.play(argv[1]);
  }

  return 0;
}