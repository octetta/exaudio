#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <math.h>
#include <stdio.h>

typedef struct {
    float amplitude;
    float baseFrequency;
    float sampleRate;
    float phase;
    float ampAttack;
    float ampDecay;
    float ampSustain;
    float ampRelease;
    float freqAttack;
    float freqDecay;
    float freqSustain;
    float freqRelease;
    float lfoFreq;
    float lfoAmpDepth;
    float lfoFreqDepth;
    float lfoPhase;
    int noteOn;
    int noteOff;
    float time;
    float releaseStartTime;
} SquareWaveOscillator;

float lfo(SquareWaveOscillator *osc) {
    osc->lfoPhase += osc->lfoFreq / osc->sampleRate;
    if (osc->lfoPhase >= 1.0f) osc->lfoPhase -= 1.0f;
    return sinf(2.0f * M_PI * osc->lfoPhase);
}

float amplitude_envelope(SquareWaveOscillator *osc) {
    float ampEnv = 0.0f;
    if (osc->noteOn) {
        if (osc->time < osc->ampAttack) {
            ampEnv = osc->time / osc->ampAttack;
        } else if (osc->time < osc->ampAttack + osc->ampDecay) {
            ampEnv = 1.0f - ((osc->time - osc->ampAttack) / osc->ampDecay) * (1.0f - osc->ampSustain);
        } else {
            ampEnv = osc->ampSustain;
        }
    } else if (osc->noteOff) {
        float t = osc->time - osc->releaseStartTime;
        if (t < osc->ampRelease) {
            ampEnv = osc->ampSustain * (1.0f - t / osc->ampRelease);
        } else {
            ampEnv = 0.0f;
        }
    }
    return ampEnv + osc->lfoAmpDepth * lfo(osc);
}

float frequency_envelope(SquareWaveOscillator *osc) {
    float freqEnv = osc->baseFrequency;
    if (osc->noteOn) {
        if (osc->time < osc->freqAttack) {
            freqEnv *= (1.0f + osc->time / osc->freqAttack);
        } else if (osc->time < osc->freqAttack + osc->freqDecay) {
            freqEnv *= (2.0f - (osc->time - osc->freqAttack) / osc->freqDecay * (1.0f - osc->freqSustain));
        } else {
            freqEnv *= osc->freqSustain;
        }
    } else if (osc->noteOff) {
        float t = osc->time - osc->releaseStartTime;
        if (t < osc->freqRelease) {
            freqEnv *= osc->freqSustain * (1.0f - t / osc->freqRelease);
        }
    }
    return freqEnv * (1.0f + osc->lfoFreqDepth * lfo(osc));
}

float square_wave(SquareWaveOscillator *osc) {
    float currentFrequency = frequency_envelope(osc);
    float value = osc->phase < 0.5f ? 1.0f : -1.0f;
    osc->phase += currentFrequency / osc->sampleRate;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;
    return value * amplitude_envelope(osc) * osc->amplitude;
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
        .baseFrequency = 440.0f,
        .sampleRate = 48000.0f,
        .phase = 0.0f,
        .ampAttack = 0.1f,
        .ampDecay = 0.2f,
        .ampSustain = 0.7f,
        .ampRelease = 0.3f,
        .freqAttack = 0.1f,
        .freqDecay = 0.2f,
        .freqSustain = 0.7f,
        .freqRelease = 0.3f,
        .lfoFreq = 5.0f, // 5 Hz LFO
        .lfoAmpDepth = 0.1f, // 10% amplitude modulation
        .lfoFreqDepth = 0.1f, // 10% frequency modulation
        .lfoPhase = 0.0f,
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
