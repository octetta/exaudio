#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define PI 3.14159265358979323846
#define MAX_VOICES 3

typedef enum {
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE,
    IDLE
} ADSRState;

typedef struct {
    float attackRate;
    float decayRate;
    float sustainLevel;
    float releaseRate;
    float envelopeLevel;
    ADSRState state;
} ADSREnvelope;

typedef struct {
    float frequency;
    float amplitude;
    float phase;
    int waveform; // 0: square, 1: sawtooth, 2: triangle
    ADSREnvelope adsr;
} SIDVoice;

typedef struct {
    SIDVoice voices[MAX_VOICES];
    uint8_t registers[25]; // Mimic SID registers (24 for voices + 1 for global control)
} SIDChip;

void update_adsr(ADSREnvelope* env, float deltaTime) {
    switch (env->state) {
        case ATTACK:
            env->envelopeLevel += env->attackRate * deltaTime;
            if (env->envelopeLevel >= 1.0) {
                env->envelopeLevel = 1.0;
                env->state = DECAY;
            }
            break;
        case DECAY:
            env->envelopeLevel -= env->decayRate * deltaTime;
            if (env->envelopeLevel <= env->sustainLevel) {
                env->envelopeLevel = env->sustainLevel;
                env->state = SUSTAIN;
            }
            break;
        case SUSTAIN:
            // Stay at sustain level
            break;
        case RELEASE:
            env->envelopeLevel -= env->releaseRate * deltaTime;
            if (env->envelopeLevel <= 0.0) {
                env->envelopeLevel = 0.0;
                env->state = IDLE;
            }
            break;
        case IDLE:
            // Do nothing
            break;
    }
}

void generate_wave(SIDVoice* voice, float* buffer, size_t frames, float deltaTime) {
    for (size_t i = 0; i < frames; i++) {
        float sample = 0.0;
        switch (voice->waveform) {
            case 0: // Square wave
                sample = sin(2 * PI * voice->frequency * voice->phase / SAMPLE_RATE) >= 0 ? 1.0 : -1.0;
                break;
            case 1: // Sawtooth wave
                sample = 2.0 * (voice->phase / SAMPLE_RATE) - 1.0;
                break;
            case 2: // Triangle wave
                sample = 2.0 * fabs(2.0 * (voice->phase / SAMPLE_RATE) - 1.0) - 1.0;
                break;
        }
        buffer[i] += sample * voice->amplitude * voice->adsr.envelopeLevel;
        voice->phase += voice->frequency;
        if (voice->phase >= SAMPLE_RATE) {
            voice->phase -= SAMPLE_RATE;
        }
        update_adsr(&voice->adsr, deltaTime);
    }
}

void apply_lowpass_filter(float* buffer, size_t frames, float cutoff, float resonance) {
    // Simple low-pass filter implementation (one-pole)
    float RC = 1.0 / (cutoff * 2 * PI);
    float dt = 1.0 / SAMPLE_RATE;
    float alpha = dt / (RC + dt);
    float previous = buffer[0];

    for (size_t i = 1; i < frames; i++) {
        buffer[i] = previous + (alpha * (buffer[i] - previous));
        previous = buffer[i];
    }
}

void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
    SIDChip* sid = (SIDChip*)device->pUserData;
    float* buffer = (float*)output;
    float deltaTime = 1.0f / SAMPLE_RATE;
    memset(buffer, 0, frameCount * sizeof(float));

    for (int v = 0; v < MAX_VOICES; v++) {
        generate_wave(&sid->voices[v], buffer, frameCount, deltaTime);
    }

    // Apply a simple low-pass filter to the entire output buffer
    apply_lowpass_filter(buffer, frameCount, 2000.0, 0.5);
}

void init_sid_chip(SIDChip* sid) {
    for (int i = 0; i < MAX_VOICES; i++) {
        sid->voices[i].frequency = 440.0;
        sid->voices[i].amplitude = 0.5;
        sid->voices[i].phase = 0.0;
        sid->voices[i].waveform = 0;
        sid->voices[i].adsr.attackRate = 0.01;
        sid->voices[i].adsr.decayRate = 0.005;
        sid->voices[i].adsr.sustainLevel = 0.7;
        sid->voices[i].adsr.releaseRate = 0.002;
        sid->voices[i].adsr.envelopeLevel = 0.0;
        sid->voices[i].adsr.state = IDLE;
    }
}

void apply_registers(SIDChip* sid) {
    for (int v = 0; v < MAX_VOICES; v++) {
        int base = v * 7;
        uint16_t freq = (sid->registers[base + 0] | (sid->registers[base + 1] << 8));
        sid->voices[v].frequency = freq * (SAMPLE_RATE / (float)(1 << 16));

        uint8_t waveCtrl = sid->registers[base + 4];
        sid->voices[v].waveform = (waveCtrl & 0xF0) >> 4;

        uint8_t attackDecay = sid->registers[base + 5];
        sid->voices[v].adsr.attackRate = (attackDecay >> 4) * 0.01;
        sid->voices[v].adsr.decayRate = (attackDecay & 0x0F) * 0.005;

        uint8_t sustainRelease = sid->registers[base + 6];
        sid->voices[v].adsr.sustainLevel = ((sustainRelease >> 4) / 15.0);
        sid->voices[v].adsr.releaseRate = (sustainRelease & 0x0F) * 0.002;

        // Check the gate bit to start/stop the ADSR envelope
        if (waveCtrl & 0x01) {
            sid->voices[v].adsr.state = ATTACK;
        } else {
            sid->voices[v].adsr.state = RELEASE;
        }
    }
}

int main() {
    SIDChip sid;
    init_sid_chip(&sid);

    // Example: Set registers to play a square wave at 440Hz with ADSR
    sid.registers[0] = 0xB0; // Frequency low byte
    sid.registers[1] = 0x42; // Frequency high byte
    sid.registers[4] = 0x11; // Waveform control (square wave, gate on)
    sid.registers[5] = 0x51; // Attack/Decay
    sid.registers[6] = 0xF2; // Sustain/Release

    apply_registers(&sid);

    ma_device_config deviceConfig;
    ma_device device;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &sid;

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

    // Example of real-time register updates
    while (getchar() != '\n') {
        // Simulate changing the frequency register
        sid.registers[0] = rand() % 256;
        sid.registers[1] = rand() % 256;

        apply_registers(&sid);
    }

    ma_device_uninit(&device);

    return 0;
}
