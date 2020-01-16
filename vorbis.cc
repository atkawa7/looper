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

#include <aacdecoder_lib.h>
#include <pthread.h>
#include <vorbis/vorbisfile.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioToolbox.h>
#include <ogg/ogg.h>
#include <stdio.h>

namespace fs = std::filesystem;

static const int kMaxNumberOfBuffers = 3;

struct AQPlayerState {
  AudioStreamBasicDescription mDataFormat;
  AudioQueueRef mQueue;
  AudioQueueBufferRef mBuffers[kMaxNumberOfBuffers];
  AudioFileID mAudioFile;
  UInt32 bufferByteSize;
  SInt64 mCurrentPacket;
  UInt32 mNumPacketsToRead;
  AudioStreamPacketDescription* mPacketDescs;
  bool mIsRunning;
  long mBytesRead;
  long mDuration;
};

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

typedef struct {
  AudioQueueRef mAudioQueue;
  AudioQueueBufferRef mAudioQueueBuffers[kMaxNumberOfBuffers];
  AudioStreamPacketDescription* mPacketDescs;
  bool mIsRunning;
  OggVorbis_File mOVFile;
  vorbis_info* mInfo;
  FILE* mOggFile;
  AudioStreamBasicDescription mDescription;
  long mBytesRead;
} EngineState;

static void outputCallback(void* aqData,
                           AudioQueueRef inAQ,
                           AudioQueueBufferRef pcmOut) {
  if (((EngineState*)aqData)->mIsRunning == 0)
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

    bytes_read = ov_read(&(((EngineState*)aqData)->mOVFile), offset, buffer_size,
                         false, 2, 1, &current_section);
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
    OSStatus status = AudioQueueEnqueueBuffer(
        ((EngineState*)aqData)->mAudioQueue, pcmOut, 0, 0);

    if (status != noErr) {
      // something has gone horribly wrong, stop now
      AudioQueueStop(((EngineState*)aqData)->mAudioQueue, true);
      ((EngineState*)aqData)->mIsRunning = false;
      printf("Error %d AudioQueueEnqueueBuffer", status);
      exit(1);
    }

    if (bytes_read == 0) {
      // we've reached EOF, let buffers drain before stopping
      AudioQueueStop(((EngineState*)aqData)->mAudioQueue, false);
      ((EngineState*)aqData)->mIsRunning = false;
    }
  }
}

int count = 0;

class Engine {
  EngineState engine_state{};

 public:
  void play(char* location) {
    engine_state.mOggFile = fopen(location, "r");
    if (engine_state.mOggFile == NULL) {
      printf("Unable to open input file.\n");
      exit(1);
    }
    // lots of assumptions for these magic parameters ... probably need to
    // detect/calculate these values at some point
    int err = (ov_open_callbacks(engine_state.mOggFile, &(engine_state.mOVFile),
                                 NULL, 0, OV_CALLBACKS_NOCLOSE));
    if (err != 0) {
      printf("Unable to set callbacks.\n");
      exit(1);
    }

    engine_state.mInfo = ov_info(&(engine_state.mOVFile), -1);
    FillOutASBDForLPCM(engine_state.mDescription, engine_state.mInfo->rate,
                       engine_state.mInfo->channels, 16, 16, false, false);
    this->prepareAudioBuffers();
    this->playAudio();
  }

  void prepareAudioBuffers() {
    OSStatus status = AudioQueueNewOutput(
        &(engine_state.mDescription), outputCallback, &engine_state,
        CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0,
        &(engine_state.mAudioQueue));

    if (status != noErr) {
      printf("Error AudioQueueNewOutput");
      exit(1);
    }

    // assume constant bit rate
    engine_state.mPacketDescs = nullptr;

    // 320kb, approx 5 seconds of stereo, 24 bit audio @ 96kHz sample rate
    static const int maxBufferSize = 0x50000;

    engine_state.mIsRunning = true;

    // allocate queues and pre-fetch some data to prime the audio buffers
    for (int i = 0; i < kMaxNumberOfBuffers; i++) {
      status = AudioQueueAllocateBuffer(engine_state.mAudioQueue, maxBufferSize,
                                        &(engine_state.mAudioQueueBuffers[i]));
      if (status != noErr) {
        AudioQueueDispose(engine_state.mAudioQueue, true);
        engine_state.mAudioQueue = 0;
        printf("Error allocating audio queue buffer %d\n", i);
        exit(1);
      }
      outputCallback(&engine_state, engine_state.mAudioQueue,
                     engine_state.mAudioQueueBuffers[i]);
    }
  }
  void playAudio() {
    // set gain and start queue

    std::cout << "Sequence \t" << ++count << std::endl;

    AudioQueueSetParameter(engine_state.mAudioQueue, kAudioQueueParam_Volume,
                           1.0);
    AudioQueueStart(engine_state.mAudioQueue, NULL);

    // maintain run loop
    do {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
    } while (engine_state.mIsRunning);

    // run a bit longer to fully flush audio buffers
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false);
    AudioQueueDispose(engine_state.mAudioQueue, true);
    ov_clear(&(engine_state.mOVFile));
    if (engine_state.mOggFile) {
      fclose(engine_state.mOggFile);
      engine_state.mOggFile = NULL;
    }
  }
};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage: vorplay <file>\n");
    exit(1);
  }

  Engine engine;
  int i = 0;
  while (i++ < kMaxNumberOfBuffers) {
    engine.play(argv[1]);
  }

  return 0;
}