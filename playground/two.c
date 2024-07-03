#include <stdio.h>
#include <math.h>
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define DURATION 5
#define TRIANGLE_FREQUENCY 440
#define SINE_FREQUENCY 660

typedef struct {
    float amplitude;
    float frequency;
    int sampleRate;
    float runningTime;
} WaveData;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    WaveData* pTriangleWaveData = ((WaveData**)pDevice->pUserData)[0];
    WaveData* pSineWaveData = ((WaveData**)pDevice->pUserData)[1];
    float* pOutputF32 = (float*)pOutput;

    for (ma_uint32 i = 0; i < frameCount; ++i) {
        // Generate triangle wave sample
        float t1 = pTriangleWaveData->runningTime;
        float period1 = 1.0f / pTriangleWaveData->frequency;
        float triangle = (2.0f / M_PI) * asinf(sinf(2.0f * M_PI * t1 / period1));

        // Generate sine wave sample
        float t2 = pSineWaveData->runningTime;
        float sine = sinf(2.0f * M_PI * pSineWaveData->frequency * t2);

        // Mix the two waveforms
        pOutputF32[i] = pTriangleWaveData->amplitude * triangle + pSineWaveData->amplitude * sine;

        // Update running times
        pTriangleWaveData->runningTime += 1.0f / pTriangleWaveData->sampleRate;
        pSineWaveData->runningTime += 1.0f / pSineWaveData->sampleRate;
    }

    (void)pInput;
}

int main() {
    ma_result result;
    ma_device_config deviceConfig;
    ma_device device;

    // Initialize wave data for triangle wave
    WaveData triangleWaveData;
    triangleWaveData.amplitude = 0.25f;
    triangleWaveData.frequency = TRIANGLE_FREQUENCY;
    triangleWaveData.sampleRate = SAMPLE_RATE;
    triangleWaveData.runningTime = 0.0f;

    // Initialize wave data for sine wave
    WaveData sineWaveData;
    sineWaveData.amplitude = 0.25f;
    sineWaveData.frequency = SINE_FREQUENCY;
    sineWaveData.sampleRate = SAMPLE_RATE;
    sineWaveData.runningTime = 0.0f;

    WaveData* waveDataArray[2] = { &triangleWaveData, &sineWaveData };

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = waveDataArray;

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
    sineWaveData.frequency = 110;
    sineWaveData.amplitude = .5;
    getchar();
    triangleWaveData.frequency = 110;
    triangleWaveData.amplitude = 1;
    getchar();
    getchar();

    ma_device_uninit(&device);

    return 0;
}
