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
    WAVEFORM_SAMPLE,
    WAVEFORM_PULSE
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
    float sampleOriginalSampleRate;
    int enableLFO;           // Flag to enable/disable LFO
    int enableFreqEnvelope;  // Flag to enable/disable frequency envelope
    int enableAmpEnvelope;   // Flag to enable/disable amplitude envelope
    float pulseWidth;        // Pulse width for pulse waveform
    int enablePWM;           // Flag to enable/disable pulse width modulation
    float pwmLfoFreq;        // Frequency of the LFO controlling pulse width
    float pwmLfoDepth;       // Depth of the LFO controlling pulse width
    float pwmLfoPhase;       // Phase of the LFO controlling pulse width
} Oscillator;

typedef struct {
    Oscillator oscillators[MAX_OSCILLATORS];
    int numOscillators;
} OscillatorSystem;

float generate_waveform(Oscillator *osc, float phase, WaveformType type) {
    if (type == WAVEFORM_PULSE && osc->enablePWM) {
        osc->pwmLfoPhase += osc->pwmLfoFreq / osc->sampleRate;
        if (osc->pwmLfoPhase >= 1.0f) osc->pwmLfoPhase -= 1.0f;
        osc->pulseWidth = 0.5f + 0.5f * osc->pwmLfoDepth * generate_waveform(osc, osc->pwmLfoPhase, osc->lfoType);
    }

    switch (type) {
        case WAVEFORM_SQUARE:
            return phase < 0.5f ? 1.0f : -1.0f;
        case WAVEFORM_SINE:
            return sinf(2.0f * M_PI * phase);
        case WAVEFORM_TRIANGLE:
            return 4.0f * fabsf(phase - 0.5f) - 1.0f;
        case WAVEFORM_SAWTOOTH:
            return 2.0f * (phase - floorf(phase + 0.5f));
        case WAVEFORM_PULSE:
            return phase < osc->pulseWidth ? 1.0f : -1.0f;
        case WAVEFORM_SAMPLE:
            if (osc->sampleData && osc->sampleFrames > 0) {
                // Adjust the phase increment according to the original sample rate
                size_t index = (size_t)(phase * osc->sampleFrames * (osc->sampleOriginalSampleRate / osc->sampleRate));
                index = index % osc->sampleFrames;
                return osc->sampleData[index];
            }
            return 0.0f;
        default:
            return 0.0f;
    }
}

float lfo(Oscillator *osc) {
    if (!osc->enableLFO) return 0.0f;
    osc->lfoPhase += osc->lfoFreq / osc->sampleRate;
    if (osc->lfoPhase >= 1.0f) osc->lfoPhase -= 1.0f;
    return generate_waveform(osc, osc->lfoPhase, osc->lfoType);
}

float amplitude_envelope(Oscillator *osc) {
    if (!osc->enableAmpEnvelope) return 1.0f;

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
    if (!osc->enableFreqEnvelope) return osc->baseFrequency;

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

float* load_sample(const char* filename, size_t* outSampleFrames, float* outSampleRate) {
    // Here you need to load the sample file and get its sample rate and sample frames.
    // For simplicity, we'll assume the sample is in raw float format with a known sample rate.
    // Replace this with actual code to load your sample correctly.

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
    *outSampleRate = 48000.0f; // Set this to the actual sample rate of your sample file

    return sampleData;
}

int main() {
    ma_result result;
    ma_device_config deviceConfig;
    ma_device device;

    OscillatorSystem oscSystem = {0};
    oscSystem.numOscillators = 16; // You can set this to any value up to MAX_OSCILLATORS

    // Load a sample file (replace "sample.raw" with your actual sample file)
    size_t sampleFrames;
    float sampleRate;
    float* sampleData = load_sample("sample.raw", &sampleFrames, &sampleRate);
    if (!sampleData) {
        return -1;
    }

    for (int i = 0; i < oscSystem.numOscillators; ++i) {
        oscSystem.oscillators[i] = (Oscillator){
            .amplitude = 0.25f,
            .baseFrequency = 440.0f + i * 10.0f, // Slightly detune each oscillator
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
            .waveformType = i == 0 ? WAVEFORM_SAMPLE : WAVEFORM_SQUARE, // First oscillator uses the sample
            .lfoType = WAVEFORM_SINE,
            .noteOn = 1,
            .noteOff = 0,
            .time = 0.0f,
            .releaseStartTime = 0.0f,
            .sampleData = sampleData,
            .sampleFrames = sampleFrames,
            .sampleOriginalSampleRate = sampleRate, // Store the original sample rate of the sample
            .enableLFO = 1, // Enable LFO by default
            .enableFreqEnvelope = 1, // Enable frequency envelope by default
            .enableAmpEnvelope = 1, // Enable amplitude envelope by default
            .pulseWidth = 0.5f, // Default pulse width
            .enablePWM = 1, // Enable PWM by default
            .pwmLfoFreq = 0.5f, // 0.5 Hz LFO for PWM
            .pwmLfoDepth = 0.5f, // 50% PWM depth
            .pwmLfoPhase = 0.0f
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

    ma_device_uninit(&device);
    free(sampleData);

    return 0;
}
