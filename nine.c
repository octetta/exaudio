#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OSCILLATORS 16
#define MAX_SAMPLE_FRAMES 48000 * 10  // Maximum sample length of 10 seconds at 48 kHz

typedef enum {
    WAVEFORM_SQUARE,
    WAVEFORM_SINE,
    WAVEFORM_TRIANGLE,
    WAVEFORM_SAWTOOTH,
    WAVEFORM_SAMPLE
} WaveformType;

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
    WaveformType waveformType;
    WaveformType lfoType;
    int noteOn;
    int noteOff;
    float time;
    float releaseStartTime;
    float* sampleData;
    size_t sampleFrames;
} Oscillator;

typedef struct {
    Oscillator oscillators[MAX_OSCILLATORS];
    int numOscillators;
} OscillatorSystem;

float generate_waveform(Oscillator *osc, float phase, WaveformType type) {
    switch (type) {
        case WAVEFORM_SQUARE:
            return phase < 0.5f ? 1.0f : -1.0f;
        case WAVEFORM_SINE:
            return sinf(2.0f * M_PI * phase);
        case WAVEFORM_TRIANGLE:
            return 4.0f * fabsf(phase - 0.5f) - 1.0f;
        case WAVEFORM_SAWTOOTH:
            return 2.0f * (phase - floorf(phase + 0.5f));
        case WAVEFORM_SAMPLE:
            if (osc->sampleData && osc->sampleFrames > 0) {
                size_t index = (size_t)(phase * osc->sampleFrames);
                index = index % osc->sampleFrames;
                return osc->sampleData[index];
            }
            return 0.0f;
        default:
            return 0.0f;
    }
}

float lfo(Oscillator *osc) {
    osc->lfoPhase += osc->lfoFreq / osc->sampleRate;
    if (osc->lfoPhase >= 1.0f) osc->lfoPhase -= 1.0f;
    return generate_waveform(osc, osc->lfoPhase, osc->lfoType);
}

float amplitude_envelope(Oscillator *osc) {
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

float frequency_envelope(Oscillator *osc) {
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

float generate_oscillator(Oscillator *osc) {
    float currentFrequency = frequency_envelope(osc);
    float value = generate_waveform(osc, osc->phase, osc->waveformType);
    osc->phase += currentFrequency / osc->sampleRate;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;
    return value * amplitude_envelope(osc) * osc->amplitude;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* pOutputF32 = (float*)pOutput;
    OscillatorSystem* oscSystem = (OscillatorSystem*)pDevice->pUserData;
    
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        float sample = 0.0f;
        for (int j = 0; j < oscSystem->numOscillators; ++j) {
            sample += generate_oscillator(&oscSystem->oscillators[j]);
        }
        sample /= oscSystem->numOscillators; // Normalize to avoid clipping
        *pOutputF32++ = sample;
        *pOutputF32++ = sample;
        for (int j = 0; j < oscSystem->numOscillators; ++j) {
            oscSystem->oscillators[j].time += 1.0f / oscSystem->oscillators[j].sampleRate;
        }
    }

    (void)pInput;
}

float* load_sample(const char* filename, size_t* outSampleFrames) {
    float *sampleData = (float *)malloc(1024 * sizeof(float));
    for (int i=0; i<1024; i++) {
        sampleData[i] = (float)(i % 128);
    }
    *outSampleFrames = 1024/2;
    return sampleData;
    #if 0
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open sample file.\n");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    float* sampleData = (float*)malloc(fileSize);
    if (!sampleData) {
        printf("Failed to allocate memory for sample data.\n");
        fclose(file);
        return NULL;
    }

    size_t framesRead = fread(sampleData, sizeof(float), fileSize / sizeof(float), file);
    fclose(file);

    *outSampleFrames = framesRead;
    return sampleData;
    #endif
}

int main() {
    ma_result result;
    ma_device_config deviceConfig;
    ma_device device;
    
    OscillatorSystem oscSystem = {0};
    oscSystem.numOscillators = 4; // You can set this to any value up to MAX_OSCILLATORS

    // Load a sample file (replace "sample.raw" with your actual sample file)
    size_t sampleFrames;
    float* sampleData = load_sample("sample.raw", &sampleFrames);
    if (!sampleData) {
        return -1;
    }

    for (int i = 0; i < oscSystem.numOscillators; ++i) {
        oscSystem.oscillators[i] = (Oscillator){
            .amplitude = 0.25f,
            .baseFrequency = 440.0f + i * 10.0f, // Slightly detune each oscillator
            .sampleRate = 48000.0f,
            .phase = 0.0f,
            .ampAttack = 10.1f * i,
            .ampDecay = 0.2f,
            .ampSustain = 0.7f,
            .ampRelease = 0.3f,
            .freqAttack = 0.1f,
            .freqDecay = 0.2f,
            .freqSustain = 0.7f,
            .freqRelease = 5.3f,
            .lfoFreq = i *1.0f + 1.0f, // 5 Hz LFO
            .lfoAmpDepth = 0.1f, // 10% amplitude modulation
            .lfoFreqDepth = 0.1f, // 10% frequency modulation
            .lfoPhase = 0.0f,
            .waveformType = i == 0 ? WAVEFORM_SAMPLE : WAVEFORM_SQUARE, // First oscillator uses the sample
            .lfoType = WAVEFORM_SINE,
            .noteOn = 1,
            .noteOff = 0,
            .time = 0.0f,
            .releaseStartTime = 0.0f,
            .sampleData = sampleData,
            .sampleFrames = sampleFrames
        };
    }

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = 48000;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = &oscSystem;

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

    printf("Press Enter to quit...\n");
    getchar();

    for (int i = 0; i < oscSystem.numOscillators; ++i) {
        oscSystem.oscillators[i].noteOff = 1;
        oscSystem.oscillators[i].noteOn = 0;
    }
    getchar();

    ma_device_uninit(&device);
    free(sampleData);

    return 0;
}
