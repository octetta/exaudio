#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  char channels;
  signed short *data;
  int len;
  int _cb_pos;
  char _cb_lock;
} audio_buffer;

void sleep_ms(int ms) {
  usleep(ms * 1000);
}

long long timespec_diff(struct timespec *begin, struct timespec *end) {
    long long diff_sec = end->tv_sec - begin->tv_sec;
    long long diff_nsec = end->tv_nsec - begin->tv_nsec;
    
    // Adjust if the difference in nanoseconds is negative
    if (diff_nsec < 0) {
        diff_sec--;
        // 1 second in nanoseconds
        diff_nsec += 1000000000;
    }

    // Convert seconds to nanoseconds
    return diff_sec * 1000000000 + diff_nsec;
}

//#define CT CLOCK_PROCESS_CPUTIME_ID
#define CT CLOCK_REALTIME

typedef struct {
    char valid;
    struct timespec t0;
    struct timespec t1;
    struct timespec t2;
    struct timespec t3;
    unsigned int cb_count;
} tracker_t;

#define STC 8
#define STM (STC-1)

tracker_t st[STC];
char sp = 0;

#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
//#define MA_DEBUG_OUTPUT
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLERATE (44100)
#define CHANNELS (2)

int buffersize = 512;

uint64_t cb_count = 0;

audio_buffer null_capture;
audio_buffer null_playback;

audio_buffer *cb_capture;
audio_buffer *cb_playback;

void exaudio_data_cb(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frame_count) {
  //clock_gettime(CT, &st[sp&STM].t0);

  if (cb_playback && cb_playback->_cb_lock && cb_playback->data) {
    int offset = cb_playback->_cb_pos;
    short int *poke = (short *)pOutput;
    for (int i=0; i<frame_count; i++) {
      if (offset > cb_playback->len) break;
      poke[i] = cb_playback->data[offset];
      offset++;
    }
    cb_playback->_cb_pos += frame_count;
    if (offset > cb_playback->len) {
      cb_playback->_cb_pos = 0;
      cb_playback->_cb_lock = 0;
    }
  }

  if (cb_capture) {
  }
  
  if (frame_count != buffersize) {
    //printf("AUGH\n");
    //return;
  }
  cb_count++;
}


void exaudio_notification_cb(const ma_device_notification* pNotification) {
  if (pNotification) {
    switch (pNotification->type) {
      case ma_device_notification_type_started:
        puts("STARTED");
        break;
      case ma_device_notification_type_stopped:
        puts("STOPPED");
        break;
      case ma_device_notification_type_rerouted:
        puts("REROUTED");
        break;
      case ma_device_notification_type_interruption_began:
        puts("INTERRUPT BEGIN");
        break;
      case ma_device_notification_type_interruption_ended:
        puts("INTERRUPT END");
        break;
      case ma_device_notification_type_unlocked:
        puts("UNLOCKED");
        break;
      default:
        printf("unknown notification %d\n", pNotification->type);
        break;
    }
  } else {
    puts("NULL NOTIFICAITON");
  }
}

ma_device_config config;
ma_device device;

unsigned char custom_data[4096];

ma_context context;

ma_device_info* pPlaybackInfos;
ma_uint32 playbackCount;

ma_device_info* pCaptureInfos;
ma_uint32 captureCount;

int playback = -1;
int pbmax = -1;
int capture = -1;
int capmax = -1;

void list_devices(void) {
  int i;

  puts("playback devices");
  for (i=0; i<playbackCount; i++) {
    printf("%d : %s\n", i, pPlaybackInfos[i].name);
    pbmax = i;
  }
  if (pbmax > 0) {
    playback = 0;
  }

  puts("capture devices");
  for (i=0; i<captureCount; i++) {
    printf("%d : %s\n", i, pCaptureInfos[i].name);
    capmax = i;
  }
}

#define STEP_MS (125)

int main(int argc, char *argv[]) {
  printf("miniaudio version %s\n", ma_version_string());

  puts("=> ma_context_init");
  sleep_ms(STEP_MS);

  if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
    printf("failed to get MA context\n");
    exit(1);
  }

  puts("=> ma_context_get_devices");
  sleep_ms(STEP_MS);

  if (ma_context_get_devices(&context,
    &pPlaybackInfos,
    &playbackCount,
    &pCaptureInfos,
    &captureCount) != MA_SUCCESS) {
    printf("failed to get I/O device list\n");
    exit(1);
  }

  list_devices();

  if (argc < 2) {
    printf("usage: %s out-device in-device\n", argv[0]);
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    return 1;
  }

  int n;

#define PLAYBACK_ARG (1)
#define CAPTURE_ARG (2)
  
  if (argc > PLAYBACK_ARG) {
    n = atoi(argv[PLAYBACK_ARG]);
    if (n >= 0 && n <= pbmax) playback = n;
  }

  if (argc > CAPTURE_ARG) {
    n = atoi(argv[CAPTURE_ARG]);
    if (n >= 0 && n <= capmax) capture = n;
  }

  printf("I/O info playback=%d capture=%d\n", playback, capture);

  puts("=> ma_device_config_init");
  sleep_ms(STEP_MS);

  if (capture < 0) {
    config = ma_device_config_init(ma_device_type_playback);
  } else {
    config = ma_device_config_init(ma_device_type_duplex);
  }

  config.playback.pDeviceID = &pPlaybackInfos[playback].id;
  if (capture >= 0) {
    config.capture.pDeviceID = &pCaptureInfos[capture].id;
  }
  config.playback.format   = ma_format_s16;
  config.playback.channels = CHANNELS;
  config.sampleRate        = SAMPLERATE;
  config.dataCallback      = exaudio_data_cb;
  config.notificationCallback = exaudio_notification_cb;
  config.pUserData         = custom_data;

  if (capture >= 0) {
    config.capture.format = ma_format_s16;
    config.capture.channels = 2;
  }

  config.periodSizeInFrames = buffersize;

  puts("=> ma_device_init");
  sleep_ms(STEP_MS);

  if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
    printf("failed to open I/O devices\n");
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    exit(1);
  }
 
  puts("=> ma_device_start");
  sleep_ms(STEP_MS);

  // start audio processing
  if (ma_device_start(&device) != MA_SUCCESS) {
    printf("Failed to start playback device.\n");
    ma_device_uninit(&device);
    exit(1);
  }

  audio_buffer wave;

#define WAVE_SEC (3)
#define WAVE_LEN (SAMPLERATE * CHANNELS * WAVE_SEC)

  signed short data[WAVE_LEN];
  wave.channels = CHANNELS;
  wave.data = (signed short *)&data;
  wave.len = WAVE_LEN;
  wave._cb_pos = 0;

  int j = 0;
  for (int i=0; i < (WAVE_LEN/CHANNELS); i+=CHANNELS) {
    data[j++] = i / 100;
    data[j++] = i / 100;
  }

  // do stuff for 60 seconds...
  for (int i=0; i<60; i++) {
    if ((i % 5) == 0) {
      puts("TRIGGER");
      if (wave._cb_lock == 0) {
        wave._cb_pos = 0;
        wave._cb_lock = 1;
        cb_playback = &wave;
      } else {
        puts("LOCKED");
      }
    }
    
    printf("%d %ld frames -> pos:%d\n",
      i,
      cb_count, wave._cb_pos);
    sleep_ms(500);
  }
  
  // stop audio processing
  puts("=> ma_device_stop");
  ma_device_stop(&device);
  sleep_ms(STEP_MS);

  puts("=> ma_device_uninit");
  sleep_ms(STEP_MS);

  ma_device_uninit(&device);

  puts("=> ma_context_uninit");
  sleep_ms(STEP_MS);

  ma_context_uninit(&context);

  puts("=> main:return");
  sleep_ms(STEP_MS);

  return 0;
}
