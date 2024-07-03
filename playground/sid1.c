#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define PI 3.14159265358979323846

typedef struct {
    float frequency;
    float amplitude;
    float phase;
    int waveform; // 0: square, 1: sawtooth, 2: triangle
} SIDVoice;

void generate_square_wave(SIDVoice* voice, float* buffer, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        buffer[i] = voice->amplitude * (sin(2 * PI * voice->frequency * voice->phase / SAMPLE_RATE) >= 0 ? 1.0 : -1.0);
        voice->phase += 1.0;
        if (voice->phase >= SAMPLE_RATE) {
            voice->phase -= SAMPLE_RATE;
        }
    }
}

void generate_sawtooth_wave(SIDVoice* voice, float* buffer, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        buffer[i] = voice->amplitude * (2.0 * (voice->phase / SAMPLE_RATE) - 1.0);
        voice->phase += voice->frequency;
        if (voice->phase >= SAMPLE_RATE) {
            voice->phase -= SAMPLE_RATE;
        }
    }
}

void generate_triangle_wave(SIDVoice* voice, float* buffer, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        buffer[i] = voice->amplitude * (2.0 * fabs(2.0 * (voice->phase / SAMPLE_RATE) - 1.0) - 1.0);
        voice->phase += voice->frequency;
        if (voice->phase >= SAMPLE_RATE) {
            voice->phase -= SAMPLE_RATE;
        }
    }
}

void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
    SIDVoice* voice = (SIDVoice*)device->pUserData;
    float* buffer = (float*)output;
    switch (voice->waveform) {
        case 0:
            generate_square_wave(voice, buffer, frameCount);
            break;
        case 1:
            generate_sawtooth_wave(voice, buffer, frameCount);
            break;
        case 2:
            generate_triangle_wave(voice, buffer, frameCount);
            break;
        default:
            break;
    }
}

int main() {
    SIDVoice voice = { 440.0, 0.5, 0.0, 0 }; // A4 note, half amplitude, square wave

    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &voice;

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Failed to initialize playback device.\n");
        return -1;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        return -1;
    }

    printf("Press Enter to quit...\n");
    getchar();

    ma_device_uninit(&device);

    return 0;
}
