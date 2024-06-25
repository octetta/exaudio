#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define I16MAX (32767)
#define I16MIN (-32767)

typedef struct {
  float *data;
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

#define OSC_COUNT (4)

audio_buffer *osc[OSC_COUNT];

void exaudio_data_cb(
  ma_device* pDevice,
  void* pOutput,
  const void* pInput,
  ma_uint32 frame_count) {

  if (frame_count != buffersize) {
    printf("AUGH\n");
    return;
  }

  float *peek = (float *)pInput;

  //clock_gettime(CT, &st[sp&STM].t0);

  playback:
  float *poke = (float *)pOutput;
  int ptr = 0;
  for (int i=0; i<frame_count; i++) {
    poke[ptr++] = 0;
    poke[ptr++] = 0;
  }
  ptr = 0;
  for (int n=0; n<OSC_COUNT; n++) {
    if (osc[n] == NULL) continue;
    // trigger sets things up and advances to playing state
    if (osc[n]->_cb_trigger) {
      osc[n]->playing = 1;
      osc[n]->_cb_trigger = 0;
    }
    if (osc[n]->playing && osc[n]->data) {
      int nptr = ptr;
      for (int i=0; i<frame_count; i++) {
        uint32_t x = (uint32_t)osc[n]->acc;
        float r = osc[n]->data[x];
        float a = poke[nptr+0];
        float b = poke[nptr+1];
        float c = a + r;
        float d = b + r;
        poke[nptr+0] = c;
        poke[nptr+1] = d;
        osc[n]->acc += osc[n]->inc;
        uint32_t iacc = (uint32_t)(osc[n]->acc + 0.5);
        uint32_t ilen = (uint32_t)osc[n]->len;
        if (osc[n]->acc > ilen) {
          if (osc[n]->loop) {
            osc[n]->acc = 0;
          } else {
            osc[n]->playing = 0;
            goto odone;
            break;
          }
        }
        nptr += 2;
      }
      odone:
    }
    ptr += 2;
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

void mkwave(audio_buffer *b, float hz, float gain, char find) {
  int duration = b->len;
  b->frequency = hz;
  b->acc = 0;
  b->inc = 1;
  b->dds_frequency = 1.0;
  float y, gy;
  // starts at 0, moving more positive
  for (int i = 0; i < duration; i++) {
    y = sin(hz * (2 * M_PI) * i / SAMPLERATE);
    gy = y * gain;
    b->data[i] = gy;
  }
  char found = 0;
  int found_index = 0;
  if (find) {
    // find last sample that crosses zero going positive
    float ly;
    char first = 1;
    for (int i = b->len-1; i >= 0; i--) {
      float cy = b->data[i];
      if (first) {
        ly = cy;
        first = 0;
        continue;
      }
      if (ly > cy) {
        if ((SIGN(ly) > 0) && (SIGN(cy) < 0)) {
          
          b->len = i;
          found = 1;
          found_index = i;
          break;
        }
      }
      ly = b->data[i];
    }
  }
  if (found) {
    printf("zero-cross downgoing at [%d]\n", found_index);
  } else {
    puts("didn't find zero-crossing downgoing");
  }
  printf("hz:%g gain:%g\n", hz, gain);
}

int exa_wave_start(audio_buffer *b) {
  if (!b) return -1;
  if (b->playing == 0) {
    b->acc = 0;
    // b->inc = (b->dds_frequency * b->len) / (float)SAMPLERATE;
    b->_cb_trigger = 1;
    return 0;
  }
  return -1;
}

int exa_wave_loop(audio_buffer *b, char loop, int limit) {
  if (!b) return -1;
  b->loop = loop;
  b->limit = limit;
  return 0;
}


int exa_wave_frequency(audio_buffer *b, float hz) {
  if (!b) return -1;
  b->dds_frequency = hz;
  b->inc = (hz * b->len) / (float)SAMPLERATE;
  return 0;
}

int exa_wave_stop(audio_buffer *b) {
  if (!b) return -1;
  b->playing = 0;
  b->loop = 0;
}

audio_buffer *exa_new_wave(int samples) {
  audio_buffer *w = (audio_buffer *)malloc(sizeof(audio_buffer));
  if (w) {
    float *data = (float *)malloc(sizeof(float) * samples);
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
  //config.playback.format   = ma_format_s16;
  config.playback.format   = ma_format_f32;
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
  float gain = 0.25;

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

  for (int i=0; i<OSC_COUNT; i++) {
    osc[i] = (audio_buffer *)NULL;
  }

  audio_buffer *wave0 = exa_new_wave(exa_samples_per_time(top, bot));
  audio_buffer *wave1 = exa_new_wave(exa_samples_per_time(top, bot));

  int loop = 0;
  int limit = 0;

  if (argc > LOOP_ARG) {
    loop = atoi(argv[LOOP_ARG]);
  }

  if (argc > LIMIT_ARG) {
    limit = atoi(argv[LIMIT_ARG]);
  }

  printf("LOOP=%d LIMIT=%d\n", loop, limit);

  // wave0->loop = loop;
  // wave0->limit = limit;

  mkwave(wave0, hz, gain, 1);
  exa_wave_frequency(wave0, 1);
  exa_wave_start(wave0);
  osc[0] = wave0;

  mkwave(wave1, hz+0.01, gain, 1);
  exa_wave_frequency(wave1, 1);
  exa_wave_loop(wave1, 1, 0);
  exa_wave_start(wave1);
  osc[1] = wave1;

  for (int i=0; i<5; i++) {
    if (i&1) exa_wave_start(wave0);
    printf("#%d (%ld cbs @ %d) -> lock:%d #:%d\n",
      i, cb_count, buffersize,
      wave0->playing,
      wave0->_cb_limit);
    sleep_ms(1000);
  }
  exa_wave_stop(wave0);

  exa_wave_start(wave0);
  for (int i=0; i<2; i++) {
    for (float f = 1; f < 2; f+=0.01) {
      exa_wave_frequency(wave0, f);
      sleep_ms(2);
    }
    for (float f = 2; f > 1; f-=0.01) {
      exa_wave_frequency(wave0, f);
      sleep_ms(2);
    }
  }
  exa_wave_stop(wave0);
  
  puts("-> 3.0");
  exa_wave_loop(wave0, 1, 0);
  exa_wave_frequency(wave0, 3);
  exa_wave_start(wave0);
  sleep_ms(2000);
  
  puts("-> 1.0");
  exa_wave_frequency(wave0, 1);
  sleep_ms(2000);
  
  puts("-> 1.01");
  exa_wave_frequency(wave0, 1.01);
  sleep_ms(2000);
  
  puts("-> 2.0");
  exa_wave_frequency(wave0, 2);
  sleep_ms(2000);
  
  puts("-> 0.5");
  exa_wave_frequency(wave0, 0.5);
  sleep_ms(2000);
  
  puts("-> 0.25");
  exa_wave_frequency(wave0, 0.25);
  sleep_ms(2000);

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
