#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  int16_t *data;
  char loop;
  int limit;
  int _cb_limit;
  char _cb_trigger;
  char playing;
  float frequency;
  float acc;
  float inc;
  float len;
  float dds_frequency;
  float gain_left;
  float gain_right;
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

int buffersize = 2048;

uint64_t cb_count = 0;

audio_buffer null_capture;
audio_buffer null_playback;

audio_buffer *cb_in;
audio_buffer *cb_out;

void exaudio_data_cb(
  ma_device* pDevice,
  void* pOutput,
  const void* pInput,
  ma_uint32 frame_count) {

  if (frame_count != buffersize) {
    printf("AUGH\n");
    return;
  }

  if (!cb_in) goto playback;
  short int *peek = (short *)pInput;

  //clock_gettime(CT, &st[sp&STM].t0);

  playback:
  if (!cb_out) goto done;
  short int *poke = (short *)pOutput;
  // trigger sets things up and advances to playing state
  if (cb_out->_cb_trigger) {
    cb_out->playing = 1;
    cb_out->_cb_trigger = 0;
    cb_out->_cb_limit = cb_out->limit;
    cb_out->acc = 0.0;
    cb_out->inc = (cb_out->dds_frequency * cb_out->len) / (float)SAMPLERATE;
  }
  if (cb_out->playing && cb_out->data) {
    int ptr = 0;
    for (int i=0; i<frame_count; i++) {
      uint32_t x = (uint32_t)cb_out->acc;
      poke[ptr++] = cb_out->data[x];
      poke[ptr++] = cb_out->data[x];
      cb_out->acc += cb_out->inc;
      if (cb_out->acc > cb_out->len) {
        if (cb_out->loop) {
          cb_out->acc = 0;
        } else {
          cb_out->playing = 0;
          goto done;
        }
      }
    }
  }
  done:
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

unsigned char custom_data[2048];

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

#define STEP_MS (10)

#define SIGN(x) ((x > 0) - (x < 0))

void mkwave(audio_buffer *b, float hz, float gain, char loop) {
  int duration = b->len;
  b->frequency = hz;
  b->dds_frequency = 1.0;
  float y, gy;
  int j = 0;
  // starts at 0, moving more positive
  for (int i = 0; i < duration; i++) {
    y = sin(hz * (2 * M_PI) * i / SAMPLERATE);
    gy = y * gain;
    int16_t iy = 32767 * gy;
    b->data[j++] = iy;
  }
  if (loop) {
    // find last sample that crosses zero going positive
    int16_t ly;
    char first = 1;
    for (int i = b->len-1; i >= 0; i--) {
      int16_t cy = b->data[i];
      if (first) {
        ly = cy;
        first = 0;
        continue;
      }
      if (ly > cy) {
        if ((SIGN(ly) > 0) && (SIGN(cy) < 0)) {
          printf("zero-cross downgoing at [%d]\n", i);
          b->len = i;
          break;
        }
      }
      ly = b->data[i];
    }
  }
  printf("hz:%g gain:%g\n", hz, gain);
}

int exa_wave_start(audio_buffer *b, int loop, int limit) {
  if (!b) return -1;
  if (b->playing == 0) {
    b->acc = 0;
    b->loop = loop;
    b->limit = limit;
    b->_cb_trigger = 1;
    cb_out = b;
    return 0;
  }
  return -1;
}

int exa_wave_stop(audio_buffer *b) {
  if (!b) return -1;
  b->playing = 0;
  b->loop = 0;
}

audio_buffer *exa_new_wave(int samples) {
  audio_buffer *w = (audio_buffer *)malloc(sizeof(audio_buffer));
  if (w) {
    int16_t *data = (int16_t *)malloc(sizeof(int16_t) * samples);
    if (data) {
      w->data = data;
      w->len = samples;
      w->len = samples;
      w->loop = 0;
      w->limit = 0;
      w->_cb_trigger = 0;
      w->_cb_limit = 0;
      w->playing = 0;
      return w;
    } else {
      free(w);
      return NULL;
    }
  }
  return NULL;
}

int exa_samples_per_time(int top, int bot) {
  return (SAMPLERATE * top) / bot;
}

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

  if (argc == 1) {
    printf("usage: %s out-device in-device\n", argv[0]);
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    return 1;
  }

  int n;

#define PLAYBACK_ARG (1)
#define CAPTURE_ARG (2)
#define HZ_ARG (3)
#define GAIN_ARG (4)
#define TOP_ARG (5)
#define BOT_ARG (6)
#define LOOP_ARG (7)
#define LIMIT_ARG (8)
  
  if (argc > PLAYBACK_ARG) {
    n = atoi(argv[PLAYBACK_ARG]);
    if (n >= 0 && n <= pbmax) playback = n;
  }

  if (argc > CAPTURE_ARG) {
    n = atoi(argv[CAPTURE_ARG]);
    if (n >= 0 && n <= capmax) capture = n;
  }

  printf("PLAYBACK=%d CAPTURE=%d\n", playback, capture);

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
    config.capture.channels = CHANNELS;
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

  float hz = 440.0;
  float gain = 0.5;

  if (argc > HZ_ARG) {
    hz = atof(argv[HZ_ARG]);
  }

  if (argc > GAIN_ARG) {
    gain = atof(argv[GAIN_ARG]);
  }

  printf("HZ=%g GAIN=%g\n", hz, gain);

  int top = 1;
  int bot = 1;

  if (argc > TOP_ARG) {
    top = atoi(argv[TOP_ARG]);
  }

  if (argc > BOT_ARG) {
    bot = atoi(argv[BOT_ARG]);
  }

  printf("TOP=%d BOT=%d (%g seconds / %d samples)\n",
    top, bot, (float)top/(float)bot,
    top * SAMPLERATE / bot);

  audio_buffer *wave = exa_new_wave(exa_samples_per_time(top, bot));

  int loop = 0;
  int limit = 0;

  if (argc > LOOP_ARG) {
    loop = atoi(argv[LOOP_ARG]);
  }

  if (argc > LIMIT_ARG) {
    limit = atoi(argv[LIMIT_ARG]);
  }

  printf("LOOP=%d LIMIT=%d\n", loop, limit);

  wave->loop = loop;
  wave->limit = limit;

  mkwave(wave, hz, gain, 1);

  printf("WAVE len = %g\n", wave->len);

  for (int i=0; i<5; i++) {

    if (i&1) exa_wave_start(wave, loop, limit);
    
    printf("#%d (%ld cbs @ %d) -> lock:%d #:%d\n",
      i, cb_count, buffersize,
      wave->playing,
      wave->_cb_limit);
    sleep_ms(1000);
  }
  exa_wave_stop(wave);

  exa_wave_start(wave, loop, limit);
  sleep_ms(2000);
  exa_wave_stop(wave);
  
  puts("-> 5.0");
  wave->dds_frequency = 5.0;
  exa_wave_start(wave, loop, limit);
  sleep_ms(2000);
  exa_wave_stop(wave);
  
  puts("-> 1.0");
  wave->dds_frequency = 1.0;
  exa_wave_start(wave, loop, limit);
  sleep_ms(2000);
  exa_wave_stop(wave);
  
  puts("-> 2.0");
  wave->dds_frequency = 2.0;
  exa_wave_start(wave, loop, limit);
  sleep_ms(2000);
  exa_wave_stop(wave);
  
  puts("-> 0.5");
  wave->dds_frequency = 0.5;
  exa_wave_start(wave, loop, limit);
  sleep_ms(2000);
  exa_wave_stop(wave);
  
  puts("-> 0.25");
  wave->dds_frequency = 0.25;
  exa_wave_start(wave, loop, limit);
  sleep_ms(2000);
  exa_wave_stop(wave);

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
