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
// #include <chrono>
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
}

#include <AudioToolbox/AudioQueue.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <CoreFoundation/CFRunLoop.h>
#include <mach/mach_time.h>
namespace fs = std::filesystem;

#define FLAC_MAX_SUPPORT_CHANNELS 2
#define BLOCKS_PER_DECODE 9216

enum DECOMPRESS_FIELDS {
  APE_INFO_FILE_VERSION =
      1000,  // version of the APE file * 1000 (3.93 = 3930) [ignored, ignored]
  APE_INFO_COMPRESSION_LEVEL =
      1001,  // compression level of the APE file [ignored, ignored]
  APE_INFO_FORMAT_FLAGS =
      1002,  // format flags of the APE file [ignored, ignored]
  APE_INFO_SAMPLE_RATE = 1003,      // sample rate (Hz) [ignored, ignored]
  APE_INFO_BITS_PER_SAMPLE = 1004,  // bits per sample [ignored, ignored]
  APE_INFO_BYTES_PER_SAMPLE =
      1005,                     // number of bytes per sample [ignored, ignored]
  APE_INFO_CHANNELS = 1006,     // channels [ignored, ignored]
  APE_INFO_BLOCK_ALIGN = 1007,  // block alignment [ignored, ignored]
  APE_INFO_BLOCKS_PER_FRAME = 1008,  // number of blocks in a frame (frames are
                                     // used internally)  [ignored, ignored]
  APE_INFO_FINAL_FRAME_BLOCKS = 1009,  // blocks in the final frame (frames are
                                       // used internally) [ignored, ignored]
  APE_INFO_TOTAL_FRAMES = 1010,        // total number frames (frames are used
                                       // internally) [ignored, ignored]
  APE_INFO_WAV_HEADER_BYTES =
      1011,  // header bytes of the decompressed WAV [ignored, ignored]
  APE_INFO_WAV_TERMINATING_BYTES =
      1012,  // terminating bytes of the decompressed WAV [ignored, ignored]
  APE_INFO_WAV_DATA_BYTES =
      1013,  // data bytes of the decompressed WAV [ignored, ignored]
  APE_INFO_WAV_TOTAL_BYTES =
      1014,  // total bytes of the decompressed WAV [ignored, ignored]
  APE_INFO_APE_TOTAL_BYTES =
      1015,  // total bytes of the APE file [ignored, ignored]
  APE_INFO_TOTAL_BLOCKS =
      1016,  // total blocks of audio data [ignored, ignored]
  APE_INFO_LENGTH_MS =
      1017,  // length in ms (1 sec = 1000 ms) [ignored, ignored]
  APE_INFO_AVERAGE_BITRATE =
      1018,  // average bitrate of the APE [ignored, ignored]
  APE_INFO_FRAME_BITRATE =
      1019,  // bitrate of specified APE frame [frame index, ignored]
  APE_INFO_DECOMPRESSED_BITRATE =
      1020,  // bitrate of the decompressed WAV [ignored, ignored]
  APE_INFO_PEAK_LEVEL =
      1021,  // peak audio level (obsolete) (-1 is unknown) [ignored, ignored]
  APE_INFO_SEEK_BIT = 1022,              // bit offset [frame index, ignored]
  APE_INFO_SEEK_BYTE = 1023,             // byte offset [frame index, ignored]
  APE_INFO_WAV_HEADER_DATA = 1024,       // error code [buffer *, max bytes]
  APE_INFO_WAV_TERMINATING_DATA = 1025,  // error code [buffer *, max bytes]
  APE_INFO_WAVEFORMATEX = 1026,          // error code [waveformatex *, ignored]
  APE_INFO_IO_SOURCE = 1027,  // I/O source (CIO *) [ignored, ignored]
  APE_INFO_FRAME_BYTES =
      1028,  // bytes (compressed) of the frame [frame index, ignored]
  APE_INFO_FRAME_BLOCKS =
      1029,             // blocks in a given frame [frame index, ignored]
  APE_INFO_TAG = 1030,  // point to tag (CAPETag *) [ignored, ignored]

  APE_DECOMPRESS_CURRENT_BLOCK =
      2000,  // current block location [ignored, ignored]
  APE_DECOMPRESS_CURRENT_MS =
      2001,  // current millisecond location [ignored, ignored]
  APE_DECOMPRESS_TOTAL_BLOCKS =
      2002,  // total blocks in the decompressors range [ignored, ignored]
  APE_DECOMPRESS_LENGTH_MS = 2003,  // length of the decompressors range in
                                    // milliseconds [ignored, ignored]
  APE_DECOMPRESS_CURRENT_BITRATE = 2004,  // current bitrate [ignored, ignored]
  APE_DECOMPRESS_AVERAGE_BITRATE =
      2005,  // average bitrate (works with ranges) [ignored, ignored]

  APE_INTERNAL_INFO = 3000,  // for internal use -- don't use (returns
                             // APE_FILE_INFO *) [ignored, ignored]
};

class ClFlacDecompress {
 public:
  ClFlacDecompress();
  virtual ~ClFlacDecompress();

  // main funcs

  virtual int GetData(char* pBuffer, int nBlocks, int* pBlocksRetrieved);
  virtual int Seek(int nBlockOffset);
  virtual int GetInfo(DECOMPRESS_FIELDS Field,
                      int nParam1 = 0,
                      int nParam2 = 0);

  // for callback

  FLAC__StreamDecoderWriteStatus Write_callback(
      const FLAC__StreamDecoder* decoder,
      const FLAC__Frame* frame,
      const FLAC__int32* const buffer[],
      void* client_data);
  void Metadata_callback(const FLAC__StreamDecoder* decoder,
                         const FLAC__StreamMetadata* metadata,
                         void* client_data);
  void Error_callback(const FLAC__StreamDecoder* decoder,
                      FLAC__StreamDecoderErrorStatus status,
                      void* client_data);

  bool playState(const char* file);
  void UnInitializeDecompressor();
  bool InitializeDecompressor(const char* file);

 protected:
  // Data

  FLAC__StreamDecoder* m_Decoder;
  FLAC__uint64 m_Total_samples;
  unsigned m_Sample_rate;
  unsigned m_nChannels;
  unsigned m_Bps;

  // buffer
  FLAC__int32 m_Buffer[FLAC_MAX_SUPPORT_CHANNELS][FLAC__MAX_BLOCK_SIZE * 2];
  FLAC__int32* m_Buffer_[FLAC_MAX_SUPPORT_CHANNELS] = {m_Buffer[0],
                                                       m_Buffer[1]};
  unsigned m_nSamplesInBuffer;
  FLAC__bool m_bAbortFlag;
};

static FLAC__StreamDecoderWriteStatus write_callback(
    const FLAC__StreamDecoder* decoder,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data) {
  ClFlacDecompress* pDecompress = static_cast<ClFlacDecompress*>(client_data);
  return pDecompress->Write_callback(decoder, frame, buffer, client_data);
}

static void metadata_callback(const FLAC__StreamDecoder* decoder,
                              const FLAC__StreamMetadata* metadata,
                              void* client_data) {
  if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    unsigned long long total_samples = metadata->data.stream_info.total_samples;
    unsigned int sample_rate = metadata->data.stream_info.sample_rate;
    unsigned int channels = metadata->data.stream_info.channels;
    unsigned int bps = metadata->data.stream_info.bits_per_sample;
    unsigned int frameSize = metadata->data.stream_info.max_framesize;
    unsigned int blockSize = metadata->data.stream_info.max_blocksize;
    fprintf(stderr, "sample rate    : %u Hz\n", sample_rate);
    fprintf(stderr, "channels       : %u\n", channels);
    fprintf(stderr, "bits per sample: %u\n", bps);
    fprintf(stderr, "MaxframeSize   : %u\n", frameSize);
    fprintf(stderr, "Max BlockSize  : %u\n", blockSize);
    fprintf(stderr, "total samples  : %llu\n", total_samples);
  } else if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    double reference, gain, peak;
    FLAC__bool album_mode = false;
    //        if (grabbag__replaygain_load_from_vorbiscomment(metadata,
    //        album_mode, false, &reference, &gain, &peak)) {
    //            fprintf(stderr, "has replaygain    : yes");
    //        } else {
    //            fprintf(stderr, "has replaygain    : no");
    //        }
  }

  ClFlacDecompress* pDecompress = static_cast<ClFlacDecompress*>(client_data);
  pDecompress->Metadata_callback(decoder, metadata, client_data);
}

static void error_callback(const FLAC__StreamDecoder* decoder,
                           FLAC__StreamDecoderErrorStatus status,
                           void* client_data) {
  ClFlacDecompress* pDecompress = static_cast<ClFlacDecompress*>(client_data);
  pDecompress->Error_callback(decoder, status, client_data);
}

#pragma mark - class define

ClFlacDecompress::ClFlacDecompress()
    : m_Decoder(NULL),
      m_Total_samples(0),
      m_Sample_rate(0),
      m_nChannels(0),
      m_Bps(0),
      m_nSamplesInBuffer(0),
      m_bAbortFlag(false) {
  if ((m_Decoder = FLAC__stream_decoder_new()) == NULL) {
    fprintf(stderr, "ERROR: allocating decoder\n");
  }
  //  bzero(m_Buffer_[0], FLAC__MAX_BLOCK_SIZE * 2);
  //  bzero(m_Buffer_[1], FLAC__MAX_BLOCK_SIZE * 2);
}

ClFlacDecompress::~ClFlacDecompress() {
  if (m_Decoder) {
    FLAC__stream_decoder_finish(m_Decoder);
    FLAC__stream_decoder_delete(m_Decoder);
    m_Decoder = NULL;
  }
}

bool ClFlacDecompress::InitializeDecompressor(const char* file) {
  if (m_Decoder == NULL || file == NULL || strlen(file) == 0) {
    return false;
  }

  if (FLAC__stream_decoder_get_state(m_Decoder) !=
      FLAC__STREAM_DECODER_UNINITIALIZED) {
    FLAC__stream_decoder_finish(m_Decoder);
  }

  FLAC__stream_decoder_set_md5_checking(m_Decoder, false);
  FLAC__stream_decoder_set_metadata_ignore_all(m_Decoder);
  FLAC__stream_decoder_set_metadata_respond(m_Decoder,
                                            FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__stream_decoder_set_metadata_respond(m_Decoder,
                                            FLAC__METADATA_TYPE_STREAMINFO);

  if (FLAC__STREAM_DECODER_INIT_STATUS_OK !=
      FLAC__stream_decoder_init_file(m_Decoder, file, write_callback,
                                     metadata_callback, error_callback, this)) {
    fprintf(stderr, "error: %s",
            FLAC__stream_decoder_get_resolved_state_string(m_Decoder));
    return false;
  }

  // Init

  m_bAbortFlag = false;
  m_Bps = 0;
  m_nChannels = 0;
  m_nSamplesInBuffer = 0;
  m_Total_samples = 0;
  m_Sample_rate = 0;

  if (false == FLAC__stream_decoder_process_until_end_of_metadata(m_Decoder)) {
    return false;
  }

  return true;
}

void ClFlacDecompress::UnInitializeDecompressor() {
  if (m_Decoder && FLAC__stream_decoder_get_state(m_Decoder) !=
                       FLAC__STREAM_DECODER_UNINITIALIZED) {
    FLAC__stream_decoder_finish(m_Decoder);
  }
}

int ClFlacDecompress::GetData(char* pBuffer,
                              int nBlocks,
                              int* pBlocksRetrieved) {
  while (m_nSamplesInBuffer < nBlocks) {
    if (FLAC__stream_decoder_get_state(m_Decoder) ==
        (FLAC__STREAM_DECODER_END_OF_STREAM)) {
      break;
    } else if (!FLAC__stream_decoder_process_single(m_Decoder)) {
      break;
    }
  }
  const unsigned retriveBlockCnt =
      (std::min)(m_nSamplesInBuffer, (unsigned)nBlocks);
  FLAC__byte* pWriter = (FLAC__byte*)pBuffer;
  int incr = m_Bps / 8 * m_nChannels;
  for (int sampleIdx = 0; sampleIdx < retriveBlockCnt; sampleIdx++) {
    switch (m_Bps) {
      case 8:
        for (int ch = 0; ch < m_nChannels; ch++) {
          pWriter[0] = m_Buffer[ch][sampleIdx] ^ 0x80;
        }
        break;
      case 24:
        for (int ch = 0; ch < m_nChannels; ch++) {
          pWriter[2] = (FLAC__byte)(m_Buffer[ch][sampleIdx] >> 16);
        }
      case 16:
        for (int ch = 0; ch < m_nChannels; ch++) {
          pWriter[0] = (FLAC__byte)(m_Buffer[ch][sampleIdx]);
          pWriter[1] = (FLAC__byte)(m_Buffer[ch][sampleIdx] >> 8);
        }
        break;
      default:
        break;
    }
    pWriter += incr;
  }
  m_nSamplesInBuffer -= retriveBlockCnt;
  for (int ch = 0; ch < m_nChannels; ch++) {
    memmove(&m_Buffer[ch][0], &m_Buffer[ch][retriveBlockCnt],
            sizeof(m_Buffer[0][0]) * retriveBlockCnt);
  }

  *pBlocksRetrieved = retriveBlockCnt;

  // printf("GetData completed\n");
  return 0;
}

#define NUM_BUFFERS 3

int ClFlacDecompress::Seek(int nBlockOffset) {
  m_nSamplesInBuffer = 0;
  if (!FLAC__stream_decoder_seek_absolute(m_Decoder, nBlockOffset)) {
    if (FLAC__stream_decoder_get_state(m_Decoder) ==
        FLAC__STREAM_DECODER_SEEK_ERROR) {
      FLAC__stream_decoder_flush(m_Decoder);
      FLAC__stream_decoder_seek_absolute(m_Decoder, 0);
    }
  }
  return 0;
}

int ClFlacDecompress::GetInfo(DECOMPRESS_FIELDS Field,
                              int nParam1,
                              int nParam2) {
  int nRetVal = 0;
  switch (Field) {
    case APE_INFO_SAMPLE_RATE:
      nRetVal = m_Sample_rate;
      break;
    case APE_INFO_CHANNELS:
      nRetVal = m_nChannels;
      break;
    case APE_INFO_BITS_PER_SAMPLE:
      nRetVal = m_Bps;
      break;
    case APE_INFO_TOTAL_BLOCKS:
      nRetVal = m_Total_samples;
      break;
    case APE_INFO_BLOCK_ALIGN:
      nRetVal = m_Bps / 8;
      break;
    case APE_INFO_BYTES_PER_SAMPLE:
      nRetVal = m_Bps / 8;
      break;
    default:
      break;
  }
  return nRetVal;
}

static void OutputCallback(void* inUserData,
                           AudioQueueRef inAQ,
                           AudioQueueBufferRef buffer);

FLAC__StreamDecoderWriteStatus ClFlacDecompress::Write_callback(
    const FLAC__StreamDecoder* decoder,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data) {
  const unsigned samplesCnt = frame->header.blocksize;
  if (m_bAbortFlag) {
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }
  for (unsigned channel = 0; channel < m_nChannels; channel++) {
    memcpy(&m_Buffer[channel][m_nSamplesInBuffer], buffer[channel],
           sizeof(buffer[0][0]) * samplesCnt);
  }
  m_nSamplesInBuffer += samplesCnt;
  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void ClFlacDecompress::Metadata_callback(const FLAC__StreamDecoder* decoder,
                                         const FLAC__StreamMetadata* metadata,
                                         void* client_data) {
  if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    m_Total_samples = metadata->data.stream_info.total_samples;
    m_Bps = metadata->data.stream_info.bits_per_sample;
    m_nChannels = metadata->data.stream_info.channels;
    m_Sample_rate = metadata->data.stream_info.sample_rate;
  }

  if (m_Bps != 8 && m_Bps != 16 && m_Bps != 24) {
    m_bAbortFlag = true;
    return;
  }
}

void ClFlacDecompress::Error_callback(const FLAC__StreamDecoder* decoder,
                                      FLAC__StreamDecoderErrorStatus status,
                                      void* client_data) {
  if (status != FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC) {
    m_bAbortFlag = true;
  }
}

typedef enum _PlayState {
  PLayStateUndefined = 0,
  PlayStateBuffering,
  // PlayStatePrepareing,
  PlayStatePlaying,
  PlayStatePaused,
  PlayStateStopped,
  PlayStateBufferingFailed,
  // PlayStateDecodeError,
  // PlayStatePlayError,
  PlayStateFailed,
  PlayStateCompleted
} PlayState;

class APEPlayer {
  AudioQueueRef m_Queue;
  AudioStreamBasicDescription m_PacketDescs;
  AudioQueueBufferRef m_Buffers[NUM_BUFFERS];
  uint32_t _totalBlocks;
  uint32_t _decodedBlocks;
  bool _isInterrpted;
  bool _workingThreadWorking;
  ClFlacDecompress* m_pDecompress;
  double m_seekTime;
  PlayState playStatePrivate;
  FILE* flog;
  std::string filePath;
  double total_duration;

 public:
  APEPlayer() {
    _isInterrpted = false;
    playStatePrivate = PLayStateUndefined;
    m_pDecompress = nullptr;
    _totalBlocks = 0;
    _decodedBlocks = 0;
    m_seekTime = 0;
    _workingThreadWorking = false;
    total_duration = 0;
  }
  ~APEPlayer() {
    if (m_pDecompress) {
      delete m_pDecompress;
      m_pDecompress = nullptr;
    }

    if (m_Queue) {
      AudioQueueDispose(m_Queue, true);
      m_Queue = 0;
    }
    if (flog) {
      std::fclose(flog);
    }
  }

  PlayState playState() { return this->playStatePrivate; }
  double schedule() {
    if (m_Queue == nullptr || m_pDecompress == nullptr) {
      return 0;
    }
    AudioTimeStamp queueTime;
    OSStatus err =
        AudioQueueGetCurrentTime(m_Queue, nullptr, &queueTime, nullptr);
    if (err != 0) {
      return 0;
    }
    double schedule =
        m_seekTime +
        queueTime.mSampleTime / m_pDecompress->GetInfo(APE_INFO_SAMPLE_RATE);
    return schedule;
  }
  void cleanBuffer() {
    if (m_Queue) {
      AudioQueueStop(m_Queue, true);
      for (int i = 0; i < NUM_BUFFERS; i++) {
        m_Buffers[i]->mAudioDataByteSize = 0;
      }
    }
    playStatePrivate = PlayStatePaused;
  }
  double duration() {
    if (m_pDecompress == NULL) {
      return 0;
    }

    // #ifdef MONKEY
    return m_pDecompress->GetInfo(APE_INFO_TOTAL_BLOCKS) /
           m_pDecompress->GetInfo(APE_INFO_SAMPLE_RATE);
    // #else
    //     return m_pDecompress->GetInfo(APE_INFO_BLOCK_ALIGN) *
    //            m_pDecompress->GetInfo(APE_INFO_TOTAL_BLOCKS) /
    //            (m_pDecompress->GetInfo(APE_INFO_SAMPLE_RATE) *
    //             m_pDecompress->GetInfo(APE_INFO_CHANNELS));
    // #endif
  };
  double playDuration() { return this->duration(); }
  float volume() { return 0.5f; }
  void setVolume(float volume) {}
  bool isPlaying() { return this->playStatePrivate == PlayStatePlaying; }
  bool isBuffering() { return false; }

  void destroy() {
    if (m_Queue) {
      AudioQueueFlush(m_Queue);
      AudioQueueStop(m_Queue, true);

      UInt32 playing = 0;
      UInt32 size = sizeof(playing);

      bool isPlaying = true;
      while (isPlaying) {
        OSStatus err = AudioQueueGetProperty(
            m_Queue, kAudioQueueProperty_IsRunning, &playing, &size);
        if (err == noErr) {
          isPlaying = (bool)(playing != 0);
        } else {
          isPlaying = false;
        }
        usleep(1000 * 10);
      }
      AudioQueueDispose(m_Queue, true);
      m_Queue = NULL;
    }
    if (m_pDecompress) {
      delete m_pDecompress;
      m_pDecompress = NULL;
    }
    m_seekTime = 0;
    playStatePrivate = PlayStateStopped;
  }

  bool create() {
    destroy();
    bool result = true;

    if (this->filePath.size() == 0) {
      this->playStatePrivate = PlayStateFailed;
      return false;
    }
    if (m_pDecompress == nullptr) {
      m_pDecompress = new ClFlacDecompress();
    }
    m_pDecompress->InitializeDecompressor(this->filePath.c_str());

    total_duration = duration();

    if (!result) {
      this->playStatePrivate = PlayStateFailed;
      return false;
    }
    int sampleRate = m_pDecompress->GetInfo(APE_INFO_SAMPLE_RATE);
    int chanel = m_pDecompress->GetInfo(APE_INFO_CHANNELS);
    int bps = m_pDecompress->GetInfo(APE_INFO_BITS_PER_SAMPLE);
    FillOutASBDForLPCM(m_PacketDescs, sampleRate, chanel, bps, bps, false,
                       false);

    OSErr n = AudioQueueNewOutput(&m_PacketDescs, OutputCallback, this,
                                  CFRunLoopGetCurrent(), kCFRunLoopCommonModes,
                                  0, &m_Queue);

    unsigned int nBufferSize = bps / 8 * BLOCKS_PER_DECODE * chanel;
    printf("nBufferSize %d\n", nBufferSize);
    for (int i = 0; i < NUM_BUFFERS && n == noErr; i++) {
      // const 0x50000 or nBufferSize
      n += AudioQueueAllocateBuffer(m_Queue, 0x50000, &m_Buffers[i]);
      printf("AudioQueueAllocateBuffer %d\n", n);

      if (n != noErr) {
        AudioQueueDispose(m_Queue, true);
        m_Queue = 0;
        printf("Error allocating audio queue buffer %d\n", i);
        exit(1);
      }

      OutputCallback(this, m_Queue, m_Buffers[i]);
    }
    return n = noErr;
  }
  bool setPlayerMediaItemInfo(const std::string& filePath) {
    printf("setPlayerMediaItemInfo from file  %s\n", filePath.c_str());
    this->filePath = filePath;

    printf("Calling destroy\n");
    this->destroy();

    printf("Calling Create \n");
    return this->create();
  }
  bool play() {
    printf("duration is %f", total_duration);
    AudioQueueSetParameter(m_Queue, kAudioQueueParam_Volume, 1.0);
    AudioQueueStart(m_Queue, nullptr);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, total_duration + 3.0, false);
    AudioQueueDispose(m_Queue, true);

    return true;
  }

  bool checkQueuePlaying(AudioQueueRef AQ) {
    if (AQ == NULL) {
      throw - 1;
    }
    OSStatus errCode = -1;
    UInt32 nData = 0;
    UInt32 nSize = sizeof(nData);
    errCode = AudioQueueGetProperty(AQ, kAudioQueueProperty_IsRunning,
                                    (void*)&nData, &nSize);
    if (errCode) {
      throw - 2;
    }
    if (nData != 0) {
      return true;
    } else {
      return false;
    }
  }
  bool pause() {
    assert(m_Queue);
    if (m_Queue == 0) {
      return true;
    }
    if (this->checkQueuePlaying(m_Queue)) {
      OSStatus n = AudioQueuePause(m_Queue);
      if (0 == n) {
        this->playStatePrivate = PlayStatePaused;
      }
    }
    return false;
  }
  bool stop() {
    this->destroy();
    return true;
  }
  bool seek(double schedule) {
    if (m_pDecompress == nullptr || m_Queue == 0 ||
        schedule > this->duration()) {
      return true;
    }
    m_seekTime = schedule;
    unsigned int nBlockOffset =
        schedule * this->m_pDecompress->GetInfo(APE_INFO_TOTAL_BLOCKS) /
        this->duration();
    this->cleanBuffer();
    this->m_pDecompress->Seek(nBlockOffset);
    this->play();
    return true;
  }
  void setPlayState(PlayState state) { this->playStatePrivate = state; }
  void onAudioInterruption(uint32_t interruptionState);
  ClFlacDecompress* getCompress() { return this->m_pDecompress; }
};

static void OutputCallback(void* inUserData,
                           AudioQueueRef inAQ,
                           AudioQueueBufferRef buffer) {
  APEPlayer* player = static_cast<APEPlayer*>(inUserData);
  int nBlockDecoded = 0;
  int result = player->getCompress()->GetData(
      (char*)buffer->mAudioData, BLOCKS_PER_DECODE, &nBlockDecoded);

  // OutputCallback
  // printf(
  //     "OutputCallback result=%d nBlockDecoded=%d inAQ=nullptr=>%d  "
  //     "buffer=nullptr=>%d\n",
  //     result, nBlockDecoded, inAQ == nullptr, buffer == nullptr);
  if (nBlockDecoded > 0 && result == 0) {
    int bufferSize = nBlockDecoded *
                     player->getCompress()->GetInfo(APE_INFO_BYTES_PER_SAMPLE) *
                     player->getCompress()->GetInfo(APE_INFO_CHANNELS);

    // printf("Buffer size = %d capacity = %d\n", bufferSize,
    //        buffer->mAudioDataBytesCapacity);
    buffer->mAudioDataByteSize = bufferSize;
    AudioQueueEnqueueBuffer(inAQ, buffer, 0, nullptr);
  } else {
    AudioQueueStop(inAQ, false);
    player->setPlayState(PlayStateStopped);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage: vorplay <file>\n");
    exit(1);
  }

  APEPlayer* aepPlayer = new APEPlayer();
  const std::string filePath(argv[1], std::strlen(argv[1]));
  int i = 0;
  while (i < 1) {
    aepPlayer->setPlayerMediaItemInfo(filePath);
    aepPlayer->play();
    i++;
  }

  return 0;
}