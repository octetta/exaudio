#include <stdio.h>
#include <math.h>
#include <stdint.h>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define NUM_VOICES 8
#define PI 3.14159265358979323846

typedef struct {
    double frequency;
    double amplitude;
    double phase;
} Voice;

typedef struct {
    Voice voices[NUM_VOICES];
    int num_active_voices;
} Synthesizer;

void init_synth(Synthesizer* synth) {
    for (int i = 0; i < NUM_VOICES; i++) {
        synth->voices[i].frequency = 0;
        synth->voices[i].amplitude = 0;
        synth->voices[i].phase = 0;
    }
    synth->num_active_voices = 0;
}

void note_on(Synthesizer* synth, double frequency, double amplitude) {
    if (synth->num_active_voices < NUM_VOICES) {
        synth->voices[synth->num_active_voices].frequency = frequency;
        synth->voices[synth->num_active_voices].amplitude = amplitude;
        synth->voices[synth->num_active_voices].phase = 0;
        synth->num_active_voices++;
    } else {
        printf("Max voices reached!\n");
    }
}

void note_off(Synthesizer* synth) {
    if (synth->num_active_voices > 0) {
        synth->num_active_voices--;
    }
}

double generate_sample(Synthesizer* synth) {
    double sample = 0;
    for (int i = 0; i < synth->num_active_voices; i++) {
        Voice* v = &synth->voices[i];
        sample += v->amplitude * sin(2 * PI * v->frequency * v->phase / SAMPLE_RATE);
        v->phase += 1;
        if (v->phase >= SAMPLE_RATE) v->phase -= SAMPLE_RATE;
    }
    return sample / synth->num_active_voices;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    Synthesizer* synth = (Synthesizer*)pDevice->pUserData;
    float* pOutputF32 = (float*)pOutput;

    for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
        double sample = generate_sample(synth);
        pOutputF32[frame] = (float)sample;
    }

    (void)pInput;
}

int main() {
    Synthesizer synth;
    init_synth(&synth);

    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &synth;

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Failed to initialize playback device.\n");
        return -1;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        return -1;
    }

    // Simulate notes being played
    note_on(&synth, 440.0, 0.5);  // A4
    ma_sleep(500);  // Half a second of audio
    note_off(&synth);

    note_on(&synth, 523.25, 0.5);  // C5
    ma_sleep(500);  // Half a second of audio
    note_off(&synth);

    ma_device_uninit(&device);

    return 0;
}
