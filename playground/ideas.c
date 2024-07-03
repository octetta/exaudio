#include <stdio.h>
#include <math.h>
#include <stdint.h>

#define SAMPLE_RATE 48000  // Sample rate in Hz
#define TABLE_SIZE 1024    // Size of the sine table
#define PI 3.14159265358979323846

// Generate a sine wave table
void generate_sine_table(float *sine_table) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        sine_table[i] = sinf(2.0 * PI * i / TABLE_SIZE);
    }
}

// DDS function to generate waveform
void dds(float frequency, float *sine_table, float *output, int num_samples) {
    static uint32_t phase_accumulator = 0;    // Phase accumulator
    uint32_t phase_increment = (uint32_t)((frequency * TABLE_SIZE) / SAMPLE_RATE * (1 << 24));

    for (int i = 0; i < num_samples; i++) {
        phase_accumulator += phase_increment;
        uint32_t table_index = (phase_accumulator >> 24) & (TABLE_SIZE - 1);
        output[i] = sine_table[table_index];
    }
}

// Function to dynamically change the frequency
void change_frequency(float new_frequency, float *sine_table, float *output, int num_samples) {
    dds(new_frequency, sine_table, output, num_samples);
}

int main() {
    float sine_table[TABLE_SIZE];
    generate_sine_table(sine_table);

    float output[SAMPLE_RATE]; // Buffer to store one second of samples

    // Initial frequency
    float frequency = 440.0; // 440 Hz (A4)
    dds(frequency, sine_table, output, SAMPLE_RATE);

    // Print the first few samples
    for (int i = 0; i < 10; i++) {
        printf("%f\n", output[i]);
    }

    // Change frequency dynamically
    frequency = 880.0; // Change to 880 Hz (A5)
    change_frequency(frequency, sine_table, output, SAMPLE_RATE);

    // Print the first few samples of the new frequency
    for (int i = 0; i < 10; i++) {
        printf("%f\n", output[i]);
    }

    return 0;
}
