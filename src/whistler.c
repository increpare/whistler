#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <fftw3.h>
#include <math.h>
#include <string.h>

// Global settings
#define MASTER_VOLUME 0.8f // Adjust this to control overall output volume

// Structure to store frequency data from windows
typedef struct {
    float frequency;
    float amplitude;
    float time;
} FrequencyPoint;

// this does fft on a running window of the audio data
// and prints the frequency with the highest amplitude
// and the amplitude of that frequency
// the window is 1024 samples long
// and the window moves 128 samples at a time

// alternative function to sinf
float triangle_wave(float x) {
    return fabs(fmod(x/4, 2.0f) - 1.0f);
}

void fft(float *buffer, int n, float *frequency, float *amplitude) {
    fftwf_complex *out;
    fftwf_plan plan;
    float *in;
    
    // Allocate memory for FFT input/output
    in = (float*) fftwf_malloc(sizeof(float) * n);
    out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * (n/2 + 1));
    
    // Copy buffer to in array
    for (int i = 0; i < n; i++) {
        in[i] = buffer[i];
    }
    
    // Create FFT plan
    plan = fftwf_plan_dft_r2c_1d(n, in, out, FFTW_ESTIMATE);
    
    // Execute FFT
    fftwf_execute(plan);
    
    // Find the bin with maximum amplitude
    float max_amplitude = 0;
    int max_bin = 0;
    
    for (int i = 0; i < n/2 + 1; i++) {
        // Calculate magnitude
        float amp = sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
        
        if (amp > max_amplitude) {
            max_amplitude = amp;
            max_bin = i;
        }
    }
    
    // Calculate frequency
    *frequency = (float)max_bin * 44100 / n;  // Assuming 44.1 kHz sample rate
    *amplitude = max_amplitude;
    
    printf("Peak frequency: %.2f Hz, Amplitude: %.2f\n", *frequency, *amplitude);
    
    // Clean up
    fftwf_destroy_plan(plan);
    fftwf_free(in);
    fftwf_free(out);
}


int main(void) {
    SF_INFO sfinfo;
    // Initialize sfinfo
    sfinfo.format = 0;  // This is important! Must be zero before calling sf_open
    
    SNDFILE *infile = sf_open("samples/test.wav", SFM_READ, &sfinfo);
    if (!infile) {
        printf("Error opening input file: %s\n", sf_strerror(NULL));
        return 1;
    }
    
    printf("Input file info:\n");
    printf("Frames: %lld\n", sfinfo.frames);
    printf("Sample rate: %d\n", sfinfo.samplerate);
    printf("Channels: %d\n", sfinfo.channels);
    printf("Format: %d\n", sfinfo.format);
    
    // Store the format information for output
    SF_INFO outinfo = sfinfo;
    outinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;  // Set output format to WAV with float encoding
    
    // Allocate buffer for the audio data
    sf_count_t items = sfinfo.frames * sfinfo.channels;
    float *buffer = malloc(items * sizeof(float));
    if (!buffer) {
        printf("Failed to allocate memory for buffer\n");
        sf_close(infile);
        return 1;
    }
    
    // Read samples into buffer (interleaved channels)
    sf_count_t read_count = sf_readf_float(infile, buffer, sfinfo.frames);
    printf("Read %lld frames of %lld\n", read_count, sfinfo.frames);
    
    sf_close(infile);

    // Perform FFT on sliding windows
    const int window_size = 1024;
    const int hop_size = 1024;//128;
    float *window_buffer = malloc(window_size * sizeof(float));
    
    if (!window_buffer) {
        printf("Failed to allocate memory for window buffer\n");
        free(buffer);
        return 1;
    }
    
    printf("\nPerforming FFT analysis:\n");
    printf("------------------------\n");
    
    // Calculate how many windows we can extract
    int num_windows = (sfinfo.frames - window_size) / hop_size + 1;
    
    // Create an array to store frequency data for each window
    FrequencyPoint *freq_data = malloc(num_windows * sizeof(FrequencyPoint));
    if (!freq_data) {
        printf("Failed to allocate memory for frequency data\n");
        free(window_buffer);
        free(buffer);
        return 1;
    }
    
    // Only process mono or first channel of stereo
    for (int w = 0; w < num_windows; w++) {
        // Fill window buffer from main buffer
        // If multi-channel, only use the first channel
        for (int i = 0; i < window_size; i++) {
            int frame_idx = w * hop_size + i;
            window_buffer[i] = buffer[frame_idx * sfinfo.channels]; // First channel only
        }
        
        // Apply Hann window function to reduce spectral leakage
        for (int i = 0; i < window_size; i++) {
            float hann = 0.5 * (1 - cosf(2 * M_PI * i / (window_size - 1)));
            window_buffer[i] *= hann;
        }
        
        // Perform FFT on the window
        float time = (float)(w * hop_size) / sfinfo.samplerate;
        printf("Window %d (time %.2f s): ", w, time);
        
        float frequency, amplitude;
        fft(window_buffer, window_size, &frequency, &amplitude);
        
        // Store the frequency data
        freq_data[w].frequency = frequency;
        freq_data[w].amplitude = amplitude / 100.0; // Scale amplitude to a reasonable value (increased from 10000.0)
        freq_data[w].time = time;
    }
    
    free(window_buffer);
    
    // Clear the buffer to prepare for sine wave generation
    memset(buffer, 0, items * sizeof(float));
    
    // Generate sine waves based on detected frequencies
    printf("\nGenerating sine wave based on detected frequencies...\n");
    
    float phase = 0.0f;
    
    // For each hop interval, use the corresponding window's frequency
    for (int w = 0; w < num_windows; w++) {
        int start_frame = w * hop_size;
        int end_frame = (w == num_windows - 1) ? sfinfo.frames : (w + 1) * hop_size;
        int segment_length = end_frame - start_frame;
        
        // Generate sine wave for this segment
        for (int i = 0; i < segment_length; i++) {
            int frame_idx = start_frame + i;
            float current_phase = phase + 2.0f * M_PI * freq_data[w].frequency * i / sfinfo.samplerate;
            
            // Apply to all channels
            for (int ch = 0; ch < sfinfo.channels; ch++) {
                buffer[frame_idx * sfinfo.channels + ch] = freq_data[w].amplitude * triangle_wave(current_phase) * MASTER_VOLUME;
            }
        }
        
        // Update phase for the next segment to ensure continuity
        phase += 2.0f * M_PI * freq_data[w].frequency * segment_length / sfinfo.samplerate;
        while (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
    
    // Free frequency data
    free(freq_data);
    
    // Save the generated sine wave to a new file using outinfo
    SNDFILE *outfile = sf_open("output.wav", SFM_WRITE, &outinfo);
    if (!outfile) {
        printf("Error opening output file: %s\n", sf_strerror(NULL));
        free(buffer);
        return 1;
    }
    
    sf_count_t write_count = sf_writef_float(outfile, buffer, sfinfo.frames);
    printf("Wrote %lld frames of %lld\n", write_count, sfinfo.frames);
    
    if (write_count != sfinfo.frames) {
        printf("Error writing to file: %s\n", sf_strerror(outfile));
    }
    
    sf_close(outfile);
    free(buffer);
    return 0;
}