#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <math.h>
#include <stdio.h>

typedef struct {
    float amplitude;
    float frequency;
    float sampleRate;
    float phase;
    float attack;
    float decay;
    float sustain;
    float release;
    int noteOn;
    int noteOff;
    float time;
    float releaseStartTime;
} SquareWaveOscillator;

float envelope(SquareWaveOscillator *osc) {
    if (osc->noteOn) {
        if (osc->time < osc->attack) {
            return osc->time / osc->attack;
        } else if (osc->time < osc->attack + osc->decay) {
            return 1.0f - ((osc->time - osc->attack) / osc->decay) * (1.0f - osc->sustain);
        } else {
            return osc->sustain;
        }
    } else if (osc->noteOff) {
        float t = osc->time - osc->releaseStartTime;
        if (t < osc->release) {
            return osc->sustain * (1.0f - t / osc->release);
        } else {
            return 0.0f;
        }
    }
    return 0.0f;
}

float square_wave(SquareWaveOscillator *osc) {
    float value = osc->phase < 0.5f ? 1.0f : -1.0f;
    osc->phase += osc->frequency / osc->sampleRate;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;
    return value * envelope(osc) * osc->amplitude;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* pOutputF32 = (float*)pOutput;
    SquareWaveOscillator* osc = (SquareWaveOscillator*)pDevice->pUserData;
    
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        *pOutputF32++ = square_wave(osc);
        *pOutputF32++ = square_wave(osc);
        osc->time += 1.0f / osc->sampleRate;
    }

    (void)pInput;
}

int main() {
    ma_result result;
    ma_device_config deviceConfig;
    ma_device device;
    
    SquareWaveOscillator osc = {
        .amplitude = 0.25f,
        .frequency = 440.0f,
        .sampleRate = 48000.0f,
        .phase = 0.0f,
        .attack = 0.1f,
        .decay = 0.2f,
        .sustain = 0.7f,
        .release = 0.3f,
        .noteOn = 1,
        .noteOff = 0,
        .time = 0.0f,
        .releaseStartTime = 0.0f
    };

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = osc.sampleRate;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = &osc;

    result = ma_device_init(NULL, &deviceConfig, &device);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize playback device.\n");
        return -1;
    }

    result = ma_device_start(&device);
    if (result != MA_SUCCESS) {
        printf("Failed to start playback device.\n");
        ma_device_uninit(&device);
        return -1;
    }

    printf("Press Enter to release the note...\n");
    getchar();
    osc.noteOn = 0;
    osc.noteOff = 1;
    osc.releaseStartTime = osc.time;

    printf("Press Enter to quit...\n");
    getchar();

    ma_device_uninit(&device);
    return 0;
}
