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

// vorplay.c - by Johann `Myrkraverk' Oskarsson <johann@myrkraverk.com>

// In the interest of example code, it's explicitly licensed under the
// WTFPL, see the bottom of the file or http://www.wtfpl.net/ for details.

#include <pthread.h>  // For pthread_exit().

#include <vorbis/vorbisfile.h>

#include <AudioToolbox/AudioToolbox.h>

#include <AudioToolbox/AudioToolbox.h>
#include <ogg/ogg.h>
#include <stdio.h>

namespace fs = std::filesystem;

const int kMaxNumberOfBuffers = 3;

typedef struct {
  AudioQueueRef mAudioQueue;
  AudioQueueBufferRef
      mAudioQueueBuffers[kMaxNumberOfBuffers];  // 3 "is typically a good
                                                // number" according to the
                                                // Apple docs
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

  long numBytesReadFromFile = 0;
  long totalBytesReadFromFile = 0;  // effectively the offset into the file

  // deals with the current logical bitstream of the vorbis file
  // currently unused here but probably needs to be supported at some point
  int currentSection = -1;

  // read from file into audio queue buffer
  do {
    numBytesReadFromFile = ov_read(
        &(((EngineState*)aqData)->mOVFile),  // OggVorbis_File structure
        (char*)pcmOut->mAudioData + totalBytesReadFromFile,  // output buffer
        pcmOut->mAudioDataBytesCapacity -
            (int)totalBytesReadFromFile,  // number of bytes to read into the
                                          // buffer
        false,                            // boolean to indicate bigendian-ness
        2,  // 1 = 8bit samples, 2 = 16bit samples; 2 is typical
        1,  // 0 for unsigned data, 1 for signed; 1 is typical
        &currentSection);

    if (numBytesReadFromFile > 0) {
      totalBytesReadFromFile += numBytesReadFromFile;
    }

  } while (totalBytesReadFromFile <= pcmOut->mAudioDataBytesCapacity &&
           numBytesReadFromFile > 0);

  if (numBytesReadFromFile <= 0) {
    if (numBytesReadFromFile == -137) {
      printf("Corrupt Bitstream; exiting");
      exit(1);
    }

    // other errors don't matter, but report in case we care
    // printf("OV_READ error %ld; continuing", numBytesReadFromFile);

    pcmOut->mAudioDataByteSize = (UInt32)totalBytesReadFromFile;
    pcmOut->mPacketDescriptionCount = 0;

    // enqueue an audio buffer after reading from disk
    OSStatus status =
        AudioQueueEnqueueBuffer(((EngineState*)aqData)->mAudioQueue, pcmOut, 0,
                                0);  // always 0 for our purposes

    if (status != noErr) {
      // something has gone horribly wrong, stop now
      AudioQueueStop(((EngineState*)aqData)->mAudioQueue, true);
      ((EngineState*)aqData)->mIsRunning = false;
      printf("Error %d AudioQueueEnqueueBuffer", status);
      exit(1);
    }

    if (numBytesReadFromFile == 0) {
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