#include <stdio.h>
#include <math.h>
#include <stdint.h>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define NUM_VOICES 8
#define PI 3.14159265358979323846

typedef enum {
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE,
    ENV_OFF
} EnvelopeState;

typedef struct {
    double attack;
    double decay;
    double sustain;
    double release;
} ADSR;

typedef struct {
    double frequency;
    double amplitude;
    double phase;
    ADSR envelope;
    EnvelopeState env_state;
    double env_level;
    double env_increment;
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
        synth->voices[i].envelope = (ADSR){0.01, 0.1, 0.7, 0.2};
        synth->voices[i].env_state = ENV_OFF;
        synth->voices[i].env_level = 0;
        synth->voices[i].env_increment = 0;
    }
    synth->num_active_voices = 0;
}

void start_envelope(Voice* voice) {
    voice->env_state = ENV_ATTACK;
    voice->env_level = 0;
    voice->env_increment = 1.0 / (voice->envelope.attack * SAMPLE_RATE);
}

void update_envelope(Voice* voice) {
    switch (voice->env_state) {
        case ENV_ATTACK:
            voice->env_level += voice->env_increment;
            if (voice->env_level >= 1.0) {
                voice->env_state = ENV_DECAY;
                voice->env_level = 1.0;
                voice->env_increment = (1.0 - voice->envelope.sustain) / (voice->envelope.decay * SAMPLE_RATE);
            }
            break;
        case ENV_DECAY:
            voice->env_level -= voice->env_increment;
            if (voice->env_level <= voice->envelope.sustain) {
                voice->env_state = ENV_SUSTAIN;
                voice->env_level = voice->envelope.sustain;
            }
            break;
        case ENV_SUSTAIN:
            break;
        case ENV_RELEASE:
            voice->env_level -= voice->env_increment;
            if (voice->env_level <= 0) {
                voice->env_state = ENV_OFF;
                voice->env_level = 0;
            }
            break;
        case ENV_OFF:
            break;
    }
}

void note_on(Synthesizer* synth, double frequency, double amplitude) {
    if (synth->num_active_voices < NUM_VOICES) {
        Voice* voice = &synth->voices[synth->num_active_voices];
        voice->frequency = frequency;
        voice->amplitude = amplitude;
        voice->phase = 0;
        start_envelope(voice);
        synth->num_active_voices++;
    } else {
        printf("Max voices reached!\n");
    }
}

void note_off(Synthesizer* synth) {
    if (synth->num_active_voices > 0) {
        Voice* voice = &synth->voices[synth->num_active_voices - 1];
        voice->env_state = ENV_RELEASE;
        voice->env_increment = voice->env_level / (voice->envelope.release * SAMPLE_RATE);
    }
}

double generate_sample(Synthesizer* synth) {
    double sample = 0;
    int active_voices = 0;

    for (int i = 0; i < synth->num_active_voices; i++) {
        Voice* voice = &synth->voices[i];
        if (voice->env_state != ENV_OFF) {
            update_envelope(voice);
            sample += voice->env_level * voice->amplitude * sin(2 * PI * voice->frequency * voice->phase / SAMPLE_RATE);
            voice->phase += 1;
            if (voice->phase >= SAMPLE_RATE) voice->phase -= SAMPLE_RATE;
            active_voices++;
        }
    }

    return active_voices > 0 ? sample / active_voices : 0;
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

void play_music(Synthesizer* synth, int bpm) {
    double seconds_per_beat = 60.0 / bpm;

    // Sequence for part 1 (simplified)
    double part1[] = {440.0, 0.0, 392.0, 0.0, 349.23, 0.0, 329.63, 0.0,
                      293.66, 0.0, 261.63, 0.0, 293.66, 0.0, 329.63, 0.0};
    int part1_length = sizeof(part1) / sizeof(double);

    // Sequence for part 2 (simplified)
    double part2[] = {392.0, 0.0, 349.23, 0.0, 329.63, 0.0, 293.66, 0.0,
                      261.63, 0.0, 293.66, 0.0, 329.63, 0.0, 349.23, 0.0};
    int part2_length = sizeof(part2) / sizeof(double);

    // Sequence for part 3 (simplified)
    double part3[] = {261.63, 0.0, 293.66, 0.0, 329.63, 0.0, 349.23, 0.0,
                      392.0, 0.0, 349.23, 0.0, 329.63, 0.0, 293.66, 0.0};
    int part3_length = sizeof(part3) / sizeof(double);

    // Sequence for part 4 (simplified)
    double part4[] = {293.66, 0.0, 329.63, 0.0, 349.23, 0.0, 392.0, 0.0,
                      523.25, 0.0, 493.88, 0.0, 466.16, 0.0, 440.0, 0.0};
    int part4_length = sizeof(part4) / sizeof(double);

    // Durations for each part (in beats)
    double durations[] = {1, 1, 1, 1, 1, 1, 1, 1};

    // Play part 1
    for (int i = 0; i < part1_length; i++) {
        if (part1[i] > 0) {
            note_on(synth, part1[i], 0.5);
        } else {
            note_off(synth);
        }
        ma_sleep((int)(seconds_per_beat * durations[i] * 1000));
    }

    // Play part 2
    for (int i = 0; i < part2_length; i++) {
        if (part2[i] > 0) {
            note_on(synth, part2[i], 0.5);
        } else {
            note_off(synth);
        }
        ma_sleep((int)(seconds_per_beat * durations[i] * 1000));
    }

    // Play part 3
    for (int i = 0; i < part3_length; i++) {
        if (part3[i] > 0) {
            note_on(synth, part3[i], 0.5);
        } else {
            note_off(synth);
        }
        ma_sleep((int)(seconds_per_beat * durations[i] * 1000));
    }

    // Play part 4
    for (int i = 0; i < part4_length; i++) {
        if (part4[i] > 0) {
            note_on(synth, part4[i], 0.5);
        } else {
            note_off(synth);
        }
        ma_sleep((int)(seconds_per_beat * durations[i] * 1000));
    }
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

    int bpm = 60;  // Set BPM to 60 for simplicity

    // Play the music
    play_music(&synth, bpm);

    ma_device_uninit(&device);

    return 0;
}
