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
    WaveformType waveformType;
    int noteOn;
    int noteOff;
    float time;
    float releaseStartTime;
    float* sampleData;
    size_t sampleFrames;
    float sampleOriginalSampleRate;
    int enableAmpEnvelope;   // Flag to enable/disable amplitude envelope
    int enableFreqEnvelope;  // Flag to enable/disable frequency envelope
    int enablePWM;           // Flag to enable/disable pulse width modulation
    float pulseWidth;        // Pulse width for pulse waveform
    int ampLfoIndex;         // Index of the oscillator used for amplitude modulation
    int freqLfoIndex;        // Index of the oscillator used for frequency modulation
    int pwmLfoIndex;         // Index of the oscillator used for pulse width modulation
    int isLFO;               // Flag to indicate if this oscillator is used as an LFO
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

float get_lfo_value(OscillatorSystem *oscSystem, int lfoIndex) {
    if (lfoIndex < 0 || lfoIndex >= oscSystem->numOscillators) {
        return 0.0f;
    }

    Oscillator *lfoOsc = &oscSystem->oscillators[lfoIndex];
    lfoOsc->phase += lfoOsc->baseFrequency / lfoOsc->sampleRate;
    if (lfoOsc->phase >= 1.0f) lfoOsc->phase -= 1.0f;
    return generate_waveform(lfoOsc, lfoOsc->phase, lfoOsc->waveformType);
}

float amplitude_envelope(OscillatorSystem *oscSystem, Oscillator *osc) {
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

    float lfoValue = get_lfo_value(oscSystem, osc->ampLfoIndex);
    return ampEnv + 0.1f * lfoValue;  // Adjust the depth as needed
}

float frequency_envelope(OscillatorSystem *oscSystem, Oscillator *osc) {
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

    float lfoValue = get_lfo_value(oscSystem, osc->freqLfoIndex);
    return freqEnv * (1.0f + 0.1f * lfoValue);  // Adjust the depth as needed
}

float generate_oscillator(OscillatorSystem *oscSystem, Oscillator *osc) {
    if (!osc->noteOn && !osc->noteOff) {
        return 0.0f;
    }

    float currentFrequency = frequency_envelope(oscSystem, osc);
    if (osc->enablePWM) {
        float lfoValue = get_lfo_value(oscSystem, osc->pwmLfoIndex);
        osc->pulseWidth = 0.5f + 0.5f * lfoValue;  // Adjust the depth as needed
    }
    float value = generate_waveform(osc, osc->phase, osc->waveformType);
    osc->phase += currentFrequency / osc->sampleRate;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;
    return value * amplitude_envelope(oscSystem, osc) * osc->amplitude;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* pOutputF32 = (float*)pOutput;
    OscillatorSystem* oscSystem = (OscillatorSystem*)pDevice->pUserData;

    for (ma_uint32 i = 0; i < frameCount; ++i) {
        float sample = 0.0f;
        for (int j = 0; j < oscSystem->numOscillators; ++j) {
            if (!oscSystem->oscillators[j].isLFO) {
                sample += generate_oscillator(oscSystem, &oscSystem->oscillators[j]);
            }
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
    oscSystem.numOscillators = 2; // You can set this to any value up to MAX_OSCILLATORS

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
            .ampAttack = 1,
            .ampDecay = 2,
            .ampSustain = 7,
            .ampRelease = 3,
            .freqAttack = 1,
            .freqDecay = 2,
            .freqSustain = 7,
            .freqRelease = 3,
            .waveformType = WAVEFORM_SAWTOOTH, // First oscillator uses the sample
            .noteOn = 0, // Set to 0 initially to start generating only when set to 1
            .noteOff = 0,
            .time = 0.0f,
            .releaseStartTime = 0.0f,
            .sampleData = sampleData,
            .sampleFrames = sampleFrames,
            .sampleOriginalSampleRate = sampleRate, // Store the original sample rate of the sample
            .enableAmpEnvelope = 1, // Enable amplitude envelope by default
            .enableFreqEnvelope = 1, // Enable frequency envelope by default
            .enablePWM = 1, // Enable PWM by default
            .pulseWidth = 0.5f, // Default pulse width
            .ampLfoIndex = -1, // No amplitude LFO by default
            .freqLfoIndex = -1, // No frequency LFO by default
            .pwmLfoIndex = -1, // No PWM LFO by default
            .isLFO = 0 // Not an LFO by default
        };
    }

    // Example: Set the first oscillator as an LFO for amplitude modulation of the second oscillator
    oscSystem.oscillators[0].isLFO = 1;
    oscSystem.oscillators[1].ampLfoIndex = 0;

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

    // Example of turning on a note after initialization
    printf("Press Enter to quit...\n");
    getchar();
    oscSystem.oscillators[1].noteOff = 0;
    oscSystem.oscillators[0].noteOff = 0;
    oscSystem.oscillators[1].noteOn = 1;
    oscSystem.oscillators[0].noteOn = 1;
    printf("Press Enter to quit...\n");
    getchar();
    oscSystem.oscillators[1].noteOn = 0;
    oscSystem.oscillators[0].noteOn = 0;
    oscSystem.oscillators[1].noteOff = 1;
    oscSystem.oscillators[0].noteOff = 1;
    printf("Press Enter to quit...\n");
    getchar();

    ma_device_uninit(&device);
    free(sampleData);

    return 0;
}
