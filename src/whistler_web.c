#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>  // For isdigit
#include <emscripten.h>

// Core settings
#define MASTER_VOLUME 0.8f
#define MIN_FREQUENCY 200.0f
#define MAX_FREQUENCY 1500.0f
#define WINDOW_SIZE 1024
#define HOP_SIZE 128
#define AMP_SCALE 200.0f
#define AMP_THRESHOLD 0.05f  // Amplitude threshold for frequency updates
#define AMP_SMOOTH 0.05f     // Amplitude smoothing factor (0-1)
#define FREQ_HYSTERESIS 0.3f // Frequency hysteresis factor (0-1)
#define NOISE_GATE 0.02f     // Noise gate threshold
#define SILENCE_COUNT 5      // Number of silent frames before completely cutting sound

// Instrument types
#define INSTR_PAD          0
#define INSTR_PLUCK        1
#define INSTR_BRASS        2
#define INSTR_FLUTE        3
#define INSTR_STRINGS      4
#define INSTR_ORGAN        5
#define INSTR_BELL         6
#define INSTR_BASS         7
#define INSTR_WURLITZER    8
#define INSTR_ACID         9

// FFT-related definitions
typedef struct {
    float real;
    float imag;
} Complex;

// FFT implementation for WebAssembly (since we can't use FFTW)
void fft(Complex* input, Complex* output, int n, int stride, int offset) {
    if (n == 1) {
        output[0].real = input[offset].real;
        output[0].imag = input[offset].imag;
        return;
    }

    fft(input, output, n/2, stride*2, offset);          // Even elements
    fft(input, output + n/2, n/2, stride*2, offset + stride); // Odd elements

    for (int k = 0; k < n/2; k++) {
        float theta = -2.0f * M_PI * k / n;
        Complex twiddle = {cosf(theta), sinf(theta)};
        
        Complex even = output[k];
        Complex odd = output[k + n/2];
        
        // t = twiddle * odd
        Complex t = {
            twiddle.real * odd.real - twiddle.imag * odd.imag,
            twiddle.real * odd.imag + twiddle.imag * odd.real
        };
        
        // output[k] = even + t
        output[k].real = even.real + t.real;
        output[k].imag = even.imag + t.imag;
        
        // output[k + n/2] = even - t
        output[k + n/2].real = even.real - t.real;
        output[k + n/2].imag = even.imag - t.imag;
    }
}

// Helper function to apply window function and prepare for FFT
void prepare_fft_input(float* audio, Complex* fft_input, int window_size) {
    for (int i = 0; i < window_size; i++) {
        // Apply Hann window
        float window = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (window_size - 1)));
        fft_input[i].real = audio[i] * window;
        fft_input[i].imag = 0.0f;
    }
}

// Find the fundamental frequency from FFT results
float find_fundamental_frequency(Complex* fft_output, int window_size, float sample_rate, float* amplitude) {
    float max_amp = 0.0f;
    int max_bin = 0;
    
    // Calculate overall signal power
    float total_power = 0.0f;
    for (int i = 1; i < window_size / 2; i++) {
        float amp = sqrtf(fft_output[i].real * fft_output[i].real + 
                          fft_output[i].imag * fft_output[i].imag);
        total_power += amp;
        
        float freq = i * sample_rate / window_size;
        if (freq < MIN_FREQUENCY || freq > MAX_FREQUENCY) continue;
        
        if (amp > max_amp) {
            max_amp = amp;
            max_bin = i;
        }
    }
    
    // Scale amplitude based on both peak and total power
    *amplitude = max_amp * AMP_SCALE;
    if (*amplitude > 1.0f) *amplitude = 1.0f;
    
    // If overall signal power is very low, reduce amplitude further
    if (total_power < 0.001f) {
        *amplitude = 0.0f;
    }
    
    // Convert bin to frequency
    if (max_bin > 0) {
        // Improve frequency estimation using peak interpolation
        int left_bin = max_bin > 1 ? max_bin - 1 : max_bin;
        int right_bin = max_bin < (window_size/2 - 1) ? max_bin + 1 : max_bin;
        
        float left_amp = sqrtf(fft_output[left_bin].real * fft_output[left_bin].real + 
                              fft_output[left_bin].imag * fft_output[left_bin].imag);
        float right_amp = sqrtf(fft_output[right_bin].real * fft_output[right_bin].real + 
                               fft_output[right_bin].imag * fft_output[right_bin].imag);
        
        // Quadratic interpolation for more accurate frequency
        float delta = 0.0f;
        if (max_bin != left_bin && max_bin != right_bin) {
            delta = 0.5f * (left_amp - right_amp) / (left_amp - 2.0f * max_amp + right_amp);
        }
        
        float interpolated_bin = max_bin + delta;
        return interpolated_bin * sample_rate / window_size;
    }
    
    return 0.0f;
}

// Utility functions for waveform generation
float soft_sine(float x) {
    float pure_sine = sinf(x);
    return pure_sine * 0.98f - 0.02f * sinf(3 * x);
}

float triangle_wave(float x) {
    const float pi = (float)M_PI;
    return 2.0f * (fabsf(fmodf(x, 2.0f * pi) - pi) - pi / 2.0f);
}

float square_wave(float x) {
    return sinf(x) >= 0.0f ? 1.0f : -1.0f;
}

float sawtooth_wave(float x) {
    return 2.0f * (fmodf(x / (2.0f * M_PI), 1.0f) - 0.5f);
}

float noise() {
    return 2.0f * ((float)rand() / RAND_MAX) - 1.0f;
}

// Blended waveform for rich pad sound
float pad_wave(float x, float blend) {
    float sine = sinf(x);
    float sine2 = sinf(x * 2.001f) * 0.3f;  // Second partial with slight detuning
    float sine3 = sinf(x * 0.5f) * 0.4f;    // Sub-oscillator for fullness
    float tri = triangle_wave(x) * 0.7f;    // Softer triangle component
    float saw = sawtooth_wave(x) * 0.5f;    // Gentler sawtooth component
    
    // Combine sine waves for a complex, rich tone
    float full_sine = sine + sine2 + sine3;
    full_sine *= 0.6f;  // Scale to avoid clipping
    
    // Create complex waveforms with softer edges
    float complex_tone = tri + saw;
    complex_tone *= 0.6f;  // Scale to avoid clipping
    
    // Blend sine-heavy tone with complex tone
    return full_sine * (1.0f - blend) + complex_tone * blend;
}

// Bell/FM waveform
float bell_wave(float x, float harmonics) {
    float carrier = sinf(x);
    float modulator = sinf(x * 2.0f) * 5.0f * harmonics;
    return sinf(x + modulator);
}

// Add harmonics for organ/brass sounds
float harmonic_wave(float x, float harmonics) {
    float result = sinf(x); // Fundamental
    float amp = 1.0f;
    
    // Add odd harmonics (organ-like)
    for (int h = 3; h <= 9; h += 2) {
        amp *= 0.5f;
        result += amp * harmonics * sinf(x * h);
    }
    
    return result / (1.0f + harmonics);
}

// Pluck/string waveform (combines harmonics)
float pluck_wave(float x, float brightness) {
    float result = 0.0f;
    float amp = 1.0f;
    
    // Add harmonics with decay based on brightness
    for (int h = 1; h <= 12; h++) {
        float harmonic_amp = amp * expf(-h * (1.0f - brightness));
        result += harmonic_amp * sinf(x * h);
        amp *= 0.7f;
    }
    
    return result * 0.3f; // Scale to avoid clipping
}

// Acid/303-style waveform with resonant filter emulation
float acid_wave(float x, float cutoff, float resonance) {
    // Basic sawtooth as the source
    float saw = sawtooth_wave(x);
    
    // Add slight phase-shifted duplicates to simulate resonance
    float resonant = saw;
    resonant += 0.4f * resonance * sawtooth_wave(x + 0.05f);
    resonant += 0.2f * resonance * sawtooth_wave(x - 0.03f);
    
    // Apply a soft clip to emulate filter distortion
    if (resonant > 0.8f) resonant = 0.8f + (resonant - 0.8f) * 0.5f;
    if (resonant < -0.8f) resonant = -0.8f + (resonant + 0.8f) * 0.5f;
    
    return resonant * cutoff; 
}

// General purpose instrument waveform selector
float instrument_wave(float x, int instrument, float wave_blend, float brightness, float harmonics) {
    switch (instrument) {
        case INSTR_PAD:
            return pad_wave(x, wave_blend);
        case INSTR_PLUCK:
            return pluck_wave(x, brightness);
        case INSTR_BRASS:
            return harmonic_wave(x, harmonics) * brightness + 
                (1.0f - brightness) * soft_sine(x);
        case INSTR_FLUTE:
            return soft_sine(x) * (1.0f - brightness) + 
                harmonic_wave(x, harmonics * 0.5f) * brightness +
                noise() * 0.02f;
        case INSTR_STRINGS:
            return pluck_wave(x, brightness * 0.7f) * 0.7f + 
                soft_sine(x) * 0.3f;
        case INSTR_ORGAN:
            return harmonic_wave(x, harmonics * 2.0f);
        case INSTR_BELL:
            return bell_wave(x, harmonics);
        case INSTR_BASS:
            // Bass combines saw and sine
            return sawtooth_wave(x) * brightness * 0.7f + 
                sinf(x) * (1.0f - brightness) + 
                sinf(x * 0.5f) * 0.3f;  // Add sub-oscillator
        case INSTR_WURLITZER:
            // Wurlitzer e-piano has a specific tone between sine and triangle
            return soft_sine(x) * (1.0f - brightness) + 
                triangle_wave(x) * brightness;
        case INSTR_ACID:
            return acid_wave(x, 0.5f + 0.5f * brightness, 0.7f + 0.3f * harmonics);
        default:
            return sinf(x);
    }
}

// Main WebAssembly-exported function for audio processing
EMSCRIPTEN_KEEPALIVE
float* process_audio(float* input_buffer, int input_length, int instrument_type, 
                    int semitones, float volume, int* output_length) {
    
    // Allocate output buffer with same length as input
    float* output_buffer = malloc(input_length * sizeof(float));
    if (!output_buffer) {
        printf("Failed to allocate memory for output buffer\n");
        *output_length = 0;
        return NULL;
    }
    
    // Clear output buffer
    memset(output_buffer, 0, input_length * sizeof(float));
    
    // Allocate memory for FFT
    Complex* fft_input = malloc(WINDOW_SIZE * sizeof(Complex));
    Complex* fft_output = malloc(WINDOW_SIZE * sizeof(Complex));
    
    if (!fft_input || !fft_output) {
        printf("Failed to allocate memory for FFT\n");
        free(output_buffer);
        free(fft_input);
        free(fft_output);
        *output_length = 0;
        return NULL;
    }
    
    // Determine sample rate based on typical web audio (44.1 kHz)
    float sample_rate = 44100.0f;
    
    // Parameters for pitch tracking and synthesis
    float current_freq = 0.0f;
    float target_freq = 0.0f;
    float current_amp = 0.0f;
    float target_amp = 0.0f;
    float phase = 0.0f;
    float last_valid_freq = 0.0f;
    int stability_counter = 0;
    int silence_counter = 0;  // Counter for consecutive silent frames
    
    // Apply semitones transposition factor
    float transpose_factor = powf(2.0f, semitones / 12.0f);
    
    // Check if input is completely silent - force complete silence in output if so
    int is_input_silent = 1;  // Assume silent until proven otherwise
    for (int i = 0; i < input_length; i++) {
        if (fabsf(input_buffer[i]) > 0.005f) {
            is_input_silent = 0;
            break;
        }
    }
    
    // If input is completely silent, just return a silent buffer
    if (is_input_silent) {
        memset(output_buffer, 0, input_length * sizeof(float));
        *output_length = input_length;
        free(fft_input);
        free(fft_output);
        return output_buffer;
    }
    
    // Process in windows
    for (int window_start = 0; window_start + WINDOW_SIZE <= input_length; window_start += HOP_SIZE) {
        // Prepare input for FFT
        prepare_fft_input(&input_buffer[window_start], fft_input, WINDOW_SIZE);
        
        // Perform FFT
        fft(fft_input, fft_output, WINDOW_SIZE, 1, 0);
        
        // Find fundamental frequency and amplitude
        float window_amp;
        float detected_freq = find_fundamental_frequency(fft_output, WINDOW_SIZE, sample_rate, &window_amp);
        
        // Calculate RMS amplitude of input signal in this window
        float input_rms = 0.0f;
        int non_zero_samples = 0;
        
        // Directly check if this window is silent
        int window_is_silent = 1;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            float sample = input_buffer[window_start + i];
            if (fabsf(sample) > 0.005f) {
                window_is_silent = 0;
                input_rms += sample * sample;
                non_zero_samples++;
            }
        }
        
        if (window_is_silent) {
            silence_counter++;
            if (silence_counter > SILENCE_COUNT) {
                // Force complete amplitude reset after multiple silent frames
                current_amp = 0.0f;
                window_amp = 0.0f;
            }
        } else {
            silence_counter = 0;
        }
        
        // Calculate RMS and apply scaling
        float input_amplitude = 0.0f;
        if (non_zero_samples > 0) {
            input_rms = sqrtf(input_rms / non_zero_samples);
            input_amplitude = input_rms * 4.0f; // Scale up to useful range
            if (input_amplitude > 1.0f) input_amplitude = 1.0f;
        }
        
        // Blend FFT-based and RMS-based amplitude
        window_amp = window_amp * 0.3f + input_amplitude * 0.7f;
        
        // Apply noise gate - zero out very low amplitudes
        if (window_amp < NOISE_GATE || window_is_silent) {
            window_amp = 0.0f;
        }
        
        // Only update frequency if amplitude is above threshold
        if (window_amp > AMP_THRESHOLD && detected_freq > MIN_FREQUENCY && detected_freq < MAX_FREQUENCY) {
            // Frequency stability check
            if (last_valid_freq > 0 && 
                (detected_freq < last_valid_freq * 0.8f || detected_freq > last_valid_freq * 1.2f)) {
                // Frequency jump detected - require more stability before accepting
                stability_counter++;
                if (stability_counter < 3) {
                    // Use the last valid frequency instead
                    detected_freq = last_valid_freq;
                } else {
                    // Accept new frequency after stability threshold
                    stability_counter = 0;
                    last_valid_freq = detected_freq;
                }
            } else {
                // Frequency is stable
                stability_counter = 0;
                last_valid_freq = detected_freq;
            }
            
            // Set target frequency with hysteresis to prevent jitter
            if (target_freq == 0.0f) {
                target_freq = detected_freq;
            } else {
                target_freq = target_freq * FREQ_HYSTERESIS + detected_freq * (1.0f - FREQ_HYSTERESIS);
            }
            
            // Set target amplitude - directly use the measured amplitude
            target_amp = window_amp;
            
            // Smooth frequency and amplitude transitions
            if (current_freq == 0.0f) {
                current_freq = target_freq;
            } else {
                // Smoother frequency transition - prevents phase artifacts
                current_freq = current_freq * 0.95f + target_freq * 0.05f;
            }
            
            // Amplitude dynamics - faster attack, slower release
            if (target_amp > current_amp) {
                // Fast attack (0.25 = attack time of about 5ms at 44.1kHz)
                current_amp = current_amp * 0.75f + target_amp * 0.25f;
            } else {
                // Slower release (0.03 = release time of about 35ms)
                current_amp = current_amp * 0.97f + target_amp * 0.03f;
            }
        } else if (current_amp > 0.0001f) {  // Much lower threshold for fade-out
            // Decay amplitude faster when below threshold
            current_amp *= 0.8f;  // Faster decay rate
            // Keep current frequency during short gaps (important for musicality)
        } else {
            // Reset when silent - ensure absolute silence
            current_amp = 0.0f;
            current_freq = 0.0f;
            target_freq = 0.0f;
            target_amp = 0.0f;
            last_valid_freq = 0.0f;
            stability_counter = 0;
            phase = 0.0f;
        }
        
        // Apply transposition to detected frequency
        float synth_freq = current_freq * transpose_factor;
        
        // Phase increment per sample based on frequency
        float phase_inc = synth_freq > 0 ? 2.0f * M_PI * synth_freq / sample_rate : 0.0f;
        
        // Generate output for this window
        for (int i = 0; i < HOP_SIZE && window_start + i < input_length; i++) {
            // Only generate sound if we have valid frequency, non-zero amplitude, and not in silence period
            if (current_freq > 0.0f && current_amp > 0.0f && silence_counter <= SILENCE_COUNT) {
                // Generate waveform based on instrument type
                float sample = instrument_wave(phase, instrument_type, 0.5f, 0.7f, 0.6f);
                
                // Square the amplitude for a more natural volume curve
                float envelope = current_amp * current_amp;
                
                // Apply volume and amplitude envelope
                sample *= envelope * volume * MASTER_VOLUME;
                
                // Apply fade-in/fade-out to reduce clicks at the window boundaries
                float window_pos = (float)i / HOP_SIZE;
                float fade = 1.0f;
                if (window_pos < 0.1f) {
                    fade = window_pos / 0.1f;  // Fade in
                } else if (window_pos > 0.9f) {
                    fade = (1.0f - window_pos) / 0.1f;  // Fade out
                }
                
                // Add to output buffer (cumulative for overlapping windows)
                output_buffer[window_start + i] += sample * fade;
                
                // Advance phase
                phase += phase_inc;
                // Keep phase within reasonable bounds
                if (phase > 1000.0f) {
                    phase = fmodf(phase, 2.0f * M_PI);
                }
            }
        }
    }
    
    // Final silence check - ensure we aren't outputting extremely quiet sounds
    // by zeroing out any samples below an absolute threshold
    for (int i = 0; i < input_length; i++) {
        if (fabsf(output_buffer[i]) < 0.0001f) {
            output_buffer[i] = 0.0f;
        }
    }
    
    // Clean up
    free(fft_input);
    free(fft_output);
    
    // Set the output length
    *output_length = input_length;
    
    return output_buffer;
}

// Simple test function that can be called from JavaScript
EMSCRIPTEN_KEEPALIVE
int get_instrument_count() {
    return 10; // Number of available instruments
}

// Return the name of an instrument
EMSCRIPTEN_KEEPALIVE
const char* get_instrument_name(int instrument_type) {
    switch (instrument_type) {
        case INSTR_PAD: return "Pad";
        case INSTR_PLUCK: return "Pluck";
        case INSTR_BRASS: return "Brass";
        case INSTR_FLUTE: return "Flute";
        case INSTR_STRINGS: return "Strings";
        case INSTR_ORGAN: return "Organ";
        case INSTR_BELL: return "Bell";
        case INSTR_BASS: return "Bass";
        case INSTR_WURLITZER: return "Wurlitzer";
        case INSTR_ACID: return "Acid";
        default: return "Unknown";
    }
}

// Main function - not used in WebAssembly context but required for compilation
int main() {
    printf("Whistler Web Audio Processor\n");
    return 0;
} 