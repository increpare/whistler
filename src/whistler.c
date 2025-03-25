#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>  // For isdigit

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Simplified versions of required functions for WebAssembly
#else
#include <sndfile.h>
#include <fftw3.h>
#endif

// Virtual file IO for WebAssembly
#ifdef __EMSCRIPTEN__
// Simplified versions of structures and functions needed for web
typedef struct {
    float* data;
    int length;
    int channels;
    int samplerate;
} AudioBuffer;

// Minimal implementation of audio processing for WebAssembly
// These functions will replace the full functionality when compiling for web
float* process_audio_emscripten(float* input_data, int input_length, int instrument_type,
                              int semitones, float volume, int* output_length);

#else
// Full implementation for native builds
typedef struct {
    sf_count_t offset;
    sf_count_t length;
    float* data;
} VirtualFile;

static sf_count_t vio_get_filelen(void* user_data) {
    VirtualFile* vf = (VirtualFile*)user_data;
    return vf->length;
}

static sf_count_t vio_seek(sf_count_t offset, int whence, void* user_data) {
    VirtualFile* vf = (VirtualFile*)user_data;
    
    switch (whence) {
        case SEEK_SET:
            vf->offset = offset;
            break;
        case SEEK_CUR:
            vf->offset += offset;
            break;
        case SEEK_END:
            vf->offset = vf->length + offset;
            break;
        default:
            return -1;
    }
    
    if (vf->offset < 0) vf->offset = 0;
    if (vf->offset > vf->length) vf->offset = vf->length;
    
    return vf->offset;
}

static sf_count_t vio_read(void* ptr, sf_count_t count, void* user_data) {
    VirtualFile* vf = (VirtualFile*)user_data;
    sf_count_t remaining = vf->length - vf->offset;
    sf_count_t to_read = (remaining < count) ? remaining : count;
    
    memcpy(ptr, &vf->data[vf->offset], to_read * sizeof(float));
    vf->offset += to_read;
    
    return to_read;
}

static sf_count_t vio_write(const void* ptr, sf_count_t count, void* user_data) {
    VirtualFile* vf = (VirtualFile*)user_data;
    sf_count_t available = vf->length - vf->offset;
    sf_count_t to_write = (available < count) ? available : count;
    
    memcpy(&vf->data[vf->offset], ptr, to_write * sizeof(float));
    vf->offset += to_write;
    
    return to_write;
}

static sf_count_t vio_tell(void* user_data) {
    VirtualFile* vf = (VirtualFile*)user_data;
    return vf->offset;
}

SF_VIRTUAL_IO sf_virtual_io = {
    vio_get_filelen,
    vio_seek,
    vio_read,
    vio_write,
    vio_tell
};
#endif

// Use the right string comparison function for the platform
#if defined(_WIN32) || defined(_WIN64)
    #define STR_COMPARE _stricmp
#else
    #define STR_COMPARE strcasecmp
#endif

// Core settings
#define MASTER_VOLUME 0.8f
#define MIN_FREQUENCY 200.0f
#define MAX_FREQUENCY 1500.0f
#define WINDOW_SIZE 1024
#define HOP_SIZE 128
#define AMP_SCALE 200.0f
#define AMP_THRESHOLD 0.05f  // Amplitude threshold for frequency updates
#define AMP_SMOOTH 0.05f     // Amplitude smoothing factor (0-1)

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

// Reverb settings - make these variables instead of macros so we can modify them
float g_reverb_mix = 0.4f;       // Mix of dry/wet (0.0 = dry, 1.0 = wet)
float g_reverb_decay = 0.8f;     // Decay factor (0.0 to 1.0)
#define REVERB_DELAY1 1567    // Prime numbers work well for delays
#define REVERB_DELAY2 2053
#define REVERB_DELAY3 3001
#define REVERB_DELAY4 4001
#define MAX_REVERB_DELAY 4001 // Maximum delay length (must be largest of the above)

// Pad synth settings
#define NUM_OSCILLATORS 4      // Number of oscillators per voice
#define DETUNE_AMOUNT 0.08f    // Detune amount in semitones
#define ATTACK_TIME 0.3f       // Attack time in seconds
#define RELEASE_TIME 0.5f      // Release time in seconds
#define OCTAVE_MIX 0.3f        // Amount of lower octave to mix in (0.0 - 1.0)
#define CHORUS_RATE 0.2f       // Chorus LFO rate in Hz
#define CHORUS_DEPTH 0.5f      // Chorus depth (0.0-1.0)
#define CHORUS_MIX 0.3f        // Chorus mix (0.0-1.0)

// Instrument presets - these will be selected based on instrument type
typedef struct {
    int num_oscillators;     // Number of oscillators
    float detune_amount;     // Detune amount in semitones
    float attack_time;       // Attack time in seconds
    float decay_time;        // Decay time in seconds
    float sustain_level;     // Sustain level (0.0-1.0)
    float release_time;      // Release time in seconds
    float octave_mix;        // Amount of lower octave to mix in
    float chorus_rate;       // Chorus LFO rate in Hz
    float chorus_depth;      // Chorus depth (0.0-1.0)
    float chorus_mix;        // Chorus mix (0.0-1.0)
    float reverb_mix;        // Reverb mix (0.0-1.0)
    float wave_blend;        // Blend between sine (0.0) and complex (1.0)
    float brightness;        // Brightness factor (filter cutoff)
    float harmonics;         // Harmonic content (0.0-1.0)
    float tremolo_rate;      // Tremolo rate in Hz
    float tremolo_depth;     // Tremolo depth (0.0-1.0)
    float filter_mod;        // Filter modulation depth
} InstrumentPreset;

// Structure to store frequency data
typedef struct {
    float frequency;
    float amplitude;
} FrequencyPoint;

// Forward declarations for all waveform functions
float triangle_wave(float x);
float pad_wave(float x, float blend);
float soft_sine(float x);
float square_wave(float x);
float sawtooth_wave(float x);
float noise(void);
float bell_wave(float x, float harmonics);
float harmonic_wave(float x, float harmonics);
float pluck_wave(float x, float brightness);
float acid_wave(float x, float cutoff, float resonance);
float instrument_wave(float x, int instrument, float wave_blend, float brightness, float harmonics);

// Forward declare the instrument presets array
extern const InstrumentPreset presets[];

// Simple sine wave generator with soft edges
float soft_sine(float x) {
    // Blend between sine and a softer waveform
    float pure_sine = sinf(x);
    // Add a small amount of the third harmonic with inverted phase
    // This reduces the harsh transitions
    return pure_sine * 0.98f - 0.02f * sinf(3 * x);
}

float triangle_wave(float x) {
    const float pi = (float)M_PI;  // Convert M_PI to float explicitly
    return 2.0f * (fabsf(fmodf(x, 2.0f * pi) - pi) - pi / 2.0f);
}

float square_wave(float x) {
    return sinf(x) >= 0.0f ? 1.0f : -1.0f;
}

float sawtooth_wave(float x) {
    return 2.0f * (fmodf(x / (2.0f * M_PI), 1.0f) - 0.5f);
}

float noise(void) {
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
    float result = 0.0f;
    
    switch (instrument) {
        case INSTR_PAD:
            return pad_wave(x, wave_blend);
            
        case INSTR_PLUCK:
            return pluck_wave(x, brightness);
            
        case INSTR_BRASS:
        case INSTR_FLUTE:
            return harmonic_wave(x, harmonics);
            
        case INSTR_STRINGS:
            // Blend of sawtooth and triangle for strings
            return sawtooth_wave(x) * 0.6f + triangle_wave(x) * 0.4f;
            
        case INSTR_ORGAN:
            // Blend square and harmonics for organ
            return square_wave(x) * 0.3f + harmonic_wave(x, harmonics) * 0.7f;
            
        case INSTR_BELL:
            return bell_wave(x, harmonics);
            
        case INSTR_BASS:
            // Deep bass sound (blend of sine and square)
            return sinf(x) * (1.0f - wave_blend) + square_wave(x) * wave_blend * 0.7f;
            
        case INSTR_WURLITZER:
            // Electric piano sound (blend of triangle and bell)
            return triangle_wave(x) * 0.6f + bell_wave(x, harmonics * 0.3f) * 0.4f;
            
        case INSTR_ACID:
            // Acid bassline with resonant filter effect
            return acid_wave(x, brightness, wave_blend);
            
        default:
            return sinf(x);
    }
}

// Convert semitones to frequency multiplier
float semitones_to_multiplier(float semitones) {
    return powf(2.0f, semitones / 12.0f);
}

// ADSR envelope
float adsr_envelope(float time, float attack, float decay, float sustain, float release, float note_length) {
    if (time < attack) {
        return time / attack; // Attack phase
    } else if (time < attack + decay) {
        return 1.0f - (1.0f - sustain) * (time - attack) / decay; // Decay phase
    } else if (time < note_length) {
        return sustain; // Sustain phase
    } else if (time < note_length + release) {
        return sustain * (1.0f - (time - note_length) / release); // Release phase
    } else {
        return 0.0f; // Note ended
    }
}

// Simple reverb implementation
void apply_reverb(float *buffer, int length, int channels) {
    float *delay_lines[4];
    int delay_lengths[4] = {REVERB_DELAY1, REVERB_DELAY2, REVERB_DELAY3, REVERB_DELAY4};
    int delay_indices[4] = {0, 0, 0, 0};
    
    // Allocate delay lines
    for (int i = 0; i < 4; i++) {
        delay_lines[i] = (float*)calloc(delay_lengths[i], sizeof(float));
        if (!delay_lines[i]) {
            printf("Failed to allocate memory for reverb\n");
            return;
        }
    }
    
    // Create a temporary buffer for the dry signal
    float *dry_buffer = (float*)malloc(length * channels * sizeof(float));
    if (!dry_buffer) {
        printf("Failed to allocate memory for reverb\n");
        return;
    }
    
    // Save the dry signal
    memcpy(dry_buffer, buffer, length * channels * sizeof(float));
    
    // Process the buffer
    for (int i = 0; i < length; i++) {
        // Get the current sample (average of all channels)
        float input = 0;
        for (int ch = 0; ch < channels; ch++) {
            input += buffer[i * channels + ch];
        }
        input /= channels;
        
        // Calculate the reverb output (feedback delay network)
        float output = 0;
        for (int j = 0; j < 4; j++) {
            // Get output from delay line
            float delay_out = delay_lines[j][delay_indices[j]];
            output += delay_out;
            
            // Update delay line with input + feedback
            delay_lines[j][delay_indices[j]] = input * 0.25f + delay_out * g_reverb_decay;
            
            // Update delay indices
            delay_indices[j] = (delay_indices[j] + 1) % delay_lengths[j];
        }
        output *= 0.5f;  // Scale the output to prevent clipping
        
        // Mix dry and wet signals
        for (int ch = 0; ch < channels; ch++) {
            buffer[i * channels + ch] = dry_buffer[i * channels + ch] * (1.0f - g_reverb_mix) + 
                                      output * g_reverb_mix;
        }
    }
    
    // Clean up
    free(dry_buffer);
    for (int i = 0; i < 4; i++) {
        free(delay_lines[i]);
    }
}

void fft(float *buffer, int n, float *frequency, float *amplitude) {
    fftwf_complex *out;
    fftwf_plan plan;
    float *in;
    
    in = (float*) fftwf_malloc(sizeof(float) * n);
    out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * (n/2 + 1));
    
    for (int i = 0; i < n; i++) {
        in[i] = buffer[i];
    }
    
    plan = fftwf_plan_dft_r2c_1d(n, in, out, FFTW_ESTIMATE);
    fftwf_execute(plan);
    
    float max_amplitude = 0;
    int max_bin = 0;
    
    for (int i = 0; i < n/2 + 1; i++) {
        float amp = sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
        if (amp > max_amplitude) {
            max_amplitude = amp;
            max_bin = i;
        }
    }
    
    *frequency = (float)max_bin * 44100 / n;
    *amplitude = max_amplitude;
    
    fftwf_destroy_plan(plan);
    fftwf_free(in);
    fftwf_free(out);
}

void print_usage(const char* program_name) {
    printf("Usage: %s <input_wav_file> [semitones] [instrument] [volume] [output_file]\n", program_name);
    printf("  input_wav_file: Path to the source WAV file\n");
    printf("  semitones: Transposition amount in semitones (positive or negative)\n");
    printf("             Default: 0 (no transposition)\n");
    printf("  instrument: Instrument type (0-9 or name)\n");
    printf("             0/pad:        Lush Pad\n");
    printf("             1/pluck:      Plucked String\n");
    printf("             2/brass:      Brass\n");
    printf("             3/flute:      Flute\n");
    printf("             4/strings:    Strings\n");
    printf("             5/organ:      Organ\n");
    printf("             6/bell:       Bell\n");
    printf("             7/bass:       Bass\n");
    printf("             8/wurlitzer:  Wurlitzer\n");
    printf("             9/acid:       Acid\n");
    printf("             Default: 0 (Pad)\n");
    printf("  volume: Output volume multiplier (0.0-10.0) (optional)\n");
    printf("             Default: 1.0 (original volume)\n");
    printf("  output_file: Path to the output WAV file (optional)\n");
    printf("             Default: <input_basename>_<instrument>_<semitones>.wav\n");
}

// Create a function to get instrument index by name
int get_instrument_by_name(const char *name) {
    const char *names[] = {
        "pad", "pluck", "brass", "flute", "strings", 
        "organ", "bell", "bass", "wurlitzer", "acid"
    };
    
    // Also accept full names with case insensitivity
    const char *full_names[] = {
        "lush pad", "plucked string", "brass", "flute", "strings", 
        "organ", "bell", "bass", "wurlitzer", "acid"
    };
    
    for (int i = 0; i < 10; i++) {
        if (STR_COMPARE(name, names[i]) == 0 || STR_COMPARE(name, full_names[i]) == 0) {
            return i;
        }
    }
    
    // Not found - try to convert to a number
    char *endptr;
    int idx = (int)strtol(name, &endptr, 10);
    
    // If conversion successful and in range, return it
    if (*name != '\0' && *endptr == '\0' && idx >= 0 && idx <= 9) {
        return idx;
    }
    
    // Invalid instrument
    return -1;
}

// Function to be exported to WebAssembly for processing audio from JavaScript
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
float* process_audio(float* input_buffer, int input_length, int instrument_type, 
                    int semitones, float volume, int* output_length) {
    
    // Create a new buffer for output (with extra space for potential effects)
    int max_output_len = input_length * 2;
    float* output_buffer = malloc(max_output_len * sizeof(float));
    if (!output_buffer) {
        printf("Failed to allocate memory for output buffer\n");
        *output_length = 0;
        return NULL;
    }
    
    // Calculate pitch shift ratio
    float pitch_ratio = powf(2.0f, semitones / 12.0f);
    
    // Apply simple instrument model and pitch shifting
    int output_pos = 0;
    for (int i = 0; i < input_length; i++) {
        // Simple time-domain pitch shifting (real implementation would use FFT)
        int source_pos = (int)(i / pitch_ratio);
        
        if (source_pos < input_length) {
            float sample = input_buffer[source_pos];
            
            // Apply volume
            sample *= volume;
            
            // Apply simple instrument effects based on instrument_type
            switch (instrument_type) {
                case 0: // Pad
                    // Add a slight chorus effect
                    if (i > 0) {
                        sample = 0.7f * sample + 0.3f * input_buffer[source_pos > 0 ? source_pos - 1 : 0];
                    }
                    break;
                case 1: // Pluck
                    // Add a slight decay
                    sample *= 1.0f - 0.2f * ((float)i / input_length);
                    break;
                case 2: // Brass
                    // Add a slight brightness
                    if (i > 0 && source_pos > 0) {
                        sample = 0.8f * sample + 0.2f * (sample - input_buffer[source_pos - 1]);
                    }
                    break;
                case 3: // Flute
                    // Add a slight smoothing
                    if (i > 0 && source_pos > 0) {
                        sample = 0.7f * sample + 0.3f * input_buffer[source_pos - 1];
                    }
                    break;
                case 4: // Strings
                    // Add a slight vibrato
                    {
                        float vibrato = 0.02f * sinf(i * 0.01f);
                        int mod_pos = source_pos + (int)(vibrato * input_length);
                        if (mod_pos >= 0 && mod_pos < input_length) {
                            sample = 0.8f * sample + 0.2f * input_buffer[mod_pos];
                        }
                    }
                    break;
                case 5: // Organ
                    // Add harmonics
                    sample = 0.7f * sample + 0.3f * sinf(i * 0.02f);
                    break;
                case 6: // Bell
                    // Add ring modulation for bell-like sound
                    sample *= (0.5f + 0.5f * sinf(i * 0.1f));
                    break;
                case 7: // Bass
                    // Enhance low end
                    sample = 1.2f * sample;
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    break;
                case 8: // Wurlitzer
                    // Add slight distortion
                    sample = tanhf(sample * 1.5f) * 0.8f;
                    break;
                case 9: // Acid
                    // Add filter sweep
                    {
                        float sweep = 0.5f + 0.5f * sinf(i * 0.001f);
                        sample = sample * sweep + 0.1f * sinf(i * 0.05f) * (1.0f - sweep);
                    }
                    break;
                default:
                    // No effect for unknown instrument types
                    break;
            }
            
            // Store in output buffer
            output_buffer[output_pos++] = sample;
        }
    }
    
    // Set the actual output length
    *output_length = output_pos;
    
    return output_buffer;
}
#else

EMSCRIPTEN_KEEPALIVE
float* process_audio(float* input_buffer, int input_length, int instrument_type, 
                    int semitones, float volume, int* output_length) {
    
    // Create a virtual file for the input
    VirtualFile vf;
    vf.data = input_buffer;
    vf.length = input_length;
    vf.offset = 0;
    
    // Create a temporary file for the input
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(sfinfo));
    sfinfo.channels = 1;
    sfinfo.samplerate = 44100;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    
    SNDFILE* input_file = sf_open_virtual(&sf_virtual_io, SFM_READ, &sfinfo, &vf);
    if (!input_file) {
        printf("Failed to create virtual input file\n");
        return NULL;
    }
    
    // Allocate output buffer
    int max_output_len = input_length * 2; // Allow extra space for effects
    float* output_buffer = malloc(max_output_len * sizeof(float));
    
    // Set up FFT for pitch detection
    fftwf_complex *fft_in, *fft_out;
    fftwf_plan fft_forward;
    
    fft_in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * WINDOW_SIZE);
    fft_out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * WINDOW_SIZE);
    fft_forward = fftwf_plan_dft_1d(WINDOW_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
    
    // Process the audio based on the instrument type
    // This is a simplified version of the main processing loop
    float current_freq = 440.0f; // Default frequency
    float current_amp = 0.0f;
    
    // Create intermediate buffer for reading audio frames
    float buffer[WINDOW_SIZE];
    int frames_read = 0;
    int output_index = 0;
    
    // Read and process frames
    while ((frames_read = sf_read_float(input_file, buffer, WINDOW_SIZE)) > 0) {
        // Apply volume
        for (int i = 0; i < frames_read; i++) {
            buffer[i] *= volume;
        }
        
        // Transpose if needed (simplified)
        if (semitones != 0) {
            float pitch_ratio = powf(2.0f, semitones / 12.0f);
            // Simple time-stretching for demonstration
            // A real implementation would use a proper time-stretching algorithm
            for (int i = 0; i < frames_read; i++) {
                int src_idx = (int)(i / pitch_ratio);
                if (src_idx < frames_read) {
                    output_buffer[output_index++] = buffer[src_idx];
                }
            }
        } else {
            // No transposition, just copy
            for (int i = 0; i < frames_read; i++) {
                output_buffer[output_index++] = buffer[i];
            }
        }
    }
    
    // Clean up FFT resources
    fftwf_destroy_plan(fft_forward);
    fftwf_free(fft_in);
    fftwf_free(fft_out);
    
    // Close the input file
    sf_close(input_file);
    
    // Set the actual output length
    *output_length = output_index;
    
    return output_buffer;
}
#endif

int main(int argc, char *argv[]) {
    // Parse command line arguments
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    float transpose_semitones = 0.0f;
    int instrument = INSTR_PAD;  // Default to pad
    float volume_multiplier = 1.0f;  // Default volume multiplier
    const char *custom_output_file = NULL;
    
    if (argc >= 3) {
        transpose_semitones = atof(argv[2]);
    }
    
    if (argc >= 4) {
        // Check if it's a name or number
        if (isdigit(argv[3][0]) || (argv[3][0] == '-' && isdigit(argv[3][1]))) {
            instrument = atoi(argv[3]);
            // Validate instrument range
            if (instrument < 0 || instrument > 9) {
                printf("Error: Instrument must be between 0 and 9\n");
                print_usage(argv[0]);
                return 1;
            }
        } else {
            // Try to get instrument by name
            instrument = get_instrument_by_name(argv[3]);
            if (instrument < 0) {
                printf("Error: Unknown instrument name: %s\n", argv[3]);
                print_usage(argv[0]);
                return 1;
            }
        }
    }
    
    if (argc >= 5) {
        volume_multiplier = atof(argv[4]);
        // Validate volume range (allow some headroom but prevent extreme values)
        if (volume_multiplier < 0.0f || volume_multiplier > 10.0f) {
            printf("Warning: Volume should be between 0.0 and 10.0. Using volume = %.1f\n", volume_multiplier);
        }
    }
    
    if (argc >= 6) {
        custom_output_file = argv[5];
    }
    
    // Get the preset for the selected instrument
    const InstrumentPreset* preset = &presets[instrument];

    // Calculate frequency multiplier from semitones
    float freq_multiplier = semitones_to_multiplier(transpose_semitones);
    printf("Transposing by %.1f semitones (multiplier: %.3f)\n", transpose_semitones, freq_multiplier);

    // Get instrument name
    const char *instrument_names[] = {
        "Lush Pad", "Plucked String", "Brass", "Flute", "Strings", 
        "Organ", "Bell", "Bass", "Wurlitzer", "Acid"
    };
    printf("Using instrument: %d - %s\n", instrument, instrument_names[instrument]);
    
    SF_INFO sfinfo;
    sfinfo.format = 0;
    
    SNDFILE *infile = sf_open(input_file, SFM_READ, &sfinfo);
    if (!infile) {
        printf("Error opening input file: %s\n", sf_strerror(NULL));
        return 1;
    }
    
    printf("Processing file: %s\n", input_file);
    printf("Sample rate: %d Hz, Channels: %d, Frames: %lld\n", 
           sfinfo.samplerate, sfinfo.channels, sfinfo.frames);

    // Allocate buffers
    sf_count_t items = sfinfo.frames * sfinfo.channels;
    float *buffer = malloc(items * sizeof(float));
    float *chorus_buffer = malloc(items * sizeof(float));
    float *window_buffer = malloc(WINDOW_SIZE * sizeof(float));
    FrequencyPoint *freq_data = malloc(((sfinfo.frames - WINDOW_SIZE) / HOP_SIZE + 1) * sizeof(FrequencyPoint));
    
    if (!buffer || !window_buffer || !freq_data || !chorus_buffer) {
        printf("Failed to allocate memory\n");
        return 1;
    }
    
    // Clear buffers
    memset(buffer, 0, items * sizeof(float));
    memset(chorus_buffer, 0, items * sizeof(float));
    
    // Read input file
    sf_readf_float(infile, buffer, sfinfo.frames);
    sf_close(infile);
    
    // Analyze audio
    int num_windows = (sfinfo.frames - WINDOW_SIZE) / HOP_SIZE + 1;
    
    float last_valid_frequency = 0.0f;
    for (int w = 0; w < num_windows; w++) {
        // Fill window buffer
        for (int i = 0; i < WINDOW_SIZE; i++) {
            window_buffer[i] = buffer[w * HOP_SIZE * sfinfo.channels + i * sfinfo.channels];
        }
        
        // Apply Hann window
        for (int i = 0; i < WINDOW_SIZE; i++) {
            float hann = 0.5 * (1 - cosf(2 * M_PI * i / (WINDOW_SIZE - 1)));
            window_buffer[i] *= hann;
        }
        
        // Perform FFT
        float frequency, amplitude;
        fft(window_buffer, WINDOW_SIZE, &frequency, &amplitude);

        // Only update frequency if amplitude is above threshold and frequency is in range
        if (amplitude > AMP_THRESHOLD && 
            frequency >= MIN_FREQUENCY && frequency <= MAX_FREQUENCY) {
            last_valid_frequency = frequency;
            // Apply transposition to the detected frequency
            freq_data[w].frequency = frequency;
        } else {
            freq_data[w].frequency = last_valid_frequency;
        }
        freq_data[w].amplitude = amplitude / AMP_SCALE;
    }
    
    // Generate output
    memset(buffer, 0, items * sizeof(float));
    float phase[NUM_OSCILLATORS] = {0};  // Phase for each oscillator
    float chorus_phase = 0.0f;           // Phase for chorus LFO
    float filter_phase = 0.0f;           // Phase for filter modulation
    float tremolo_phase = 0.0f;          // Phase for tremolo
    float current_frequency = freq_data[0].frequency;
    float smooth_amp = 0.0f;  // Start with zero amplitude
    
    // Get preset values for more readable code
    int num_oscillators = preset->num_oscillators;
    float detune_amount = preset->detune_amount;
    float attack_time = preset->attack_time;
    float decay_time = preset->decay_time;
    float sustain_level = preset->sustain_level;
    float release_time = preset->release_time;
    float octave_mix = preset->octave_mix;
    float chorus_rate = preset->chorus_rate;
    float chorus_depth = preset->chorus_depth;
    float chorus_mix = preset->chorus_mix;
    float reverb_mix = preset->reverb_mix;
    float wave_blend = preset->wave_blend;
    float brightness = preset->brightness;
    float harmonics = preset->harmonics;
    float tremolo_rate = preset->tremolo_rate;
    float tremolo_depth = preset->tremolo_depth;
    float filter_mod = preset->filter_mod;
    
    for (int w = 0; w < num_windows; w++) {
        int start_frame = w * HOP_SIZE;
        int end_frame = (w == num_windows - 1) ? sfinfo.frames : (w + 1) * HOP_SIZE;
        
        // Keep current frequency if amplitude is below threshold
        float next_frequency = current_frequency;
        if (freq_data[w].amplitude > AMP_THRESHOLD && w < num_windows - 1) {
            next_frequency = freq_data[w + 1].frequency;
        }
        
        for (int i = 0; i < end_frame - start_frame; i++) {
            int current_sample = start_frame + i;
            float progress = (float)i / (end_frame - start_frame);
            float frequency = current_frequency * (1.0f - progress) + next_frequency * progress;
            
            // Apply frequency multiplier (transposition)
            float transposed_freq = frequency * freq_multiplier;
            
            // Smooth amplitude transitions
            smooth_amp = smooth_amp * (1.0f - AMP_SMOOTH) + freq_data[w].amplitude * AMP_SMOOTH;
            
            // Calculate envelope
            float env_time = (float)current_sample / sfinfo.samplerate;
            float note_length = (float)sfinfo.frames / sfinfo.samplerate;

            // Ensure release phase starts at an appropriate time, especially for long release times
            float release_start = note_length - release_time * 1.5f;
            if (release_start < attack_time + decay_time) {
                // If the file is very short, adjust to ensure we still hear something
                release_start = attack_time + decay_time + 0.1f;
            }

            float envelope = adsr_envelope(env_time, attack_time, decay_time, sustain_level, release_time, release_start);
            
            // Update chorus LFO
            float chorus_lfo_rate = 2.0f * M_PI * chorus_rate / sfinfo.samplerate;
            chorus_phase += chorus_lfo_rate;
            if (chorus_phase >= 2.0f * M_PI) chorus_phase -= 2.0f * M_PI;
            float chorus_mod = chorus_depth * sinf(chorus_phase);
            
            // Update filter modulation
            float filter_lfo_rate = 2.0f * M_PI * 0.1f / sfinfo.samplerate;  // 0.1 Hz filter sweep
            filter_phase += filter_lfo_rate;
            if (filter_phase >= 2.0f * M_PI) filter_phase -= 2.0f * M_PI;
            float filter_mod_amount = 0.5f + 0.5f * sinf(filter_phase) * filter_mod;
            
            // Update tremolo if used
            float tremolo_amount = 1.0f;
            if (tremolo_rate > 0.0f) {
                float tremolo_lfo_rate = 2.0f * M_PI * tremolo_rate / sfinfo.samplerate;
                tremolo_phase += tremolo_lfo_rate;
                if (tremolo_phase >= 2.0f * M_PI) tremolo_phase -= 2.0f * M_PI;
                tremolo_amount = 1.0f - tremolo_depth * (0.5f + 0.5f * sinf(tremolo_phase));
            }
            
            // Generate multi-oscillator sound
            float sample = 0.0f;
            for (int osc = 0; osc < num_oscillators; osc++) {
                // Calculate detune factor based on oscillator index
                float detune_factor = 1.0f;
                if (osc == 0) {
                    detune_factor = 1.0f;  // Root note
                } else if (osc == 1 && num_oscillators > 1) {
                    detune_factor = semitones_to_multiplier(detune_amount);  // Slightly sharp
                } else if (osc == 2 && num_oscillators > 2) {
                    detune_factor = semitones_to_multiplier(-detune_amount); // Slightly flat
                } else if (osc == 3 && num_oscillators > 3) {
                    detune_factor = 0.5f;  // Octave below
                }
                
                // Update phase for this oscillator
                float phase_increment = 2.0f * M_PI * (transposed_freq * detune_factor) / sfinfo.samplerate;
                phase[osc] += phase_increment;
                while (phase[osc] >= 2.0f * M_PI) phase[osc] -= 2.0f * M_PI;
                
                // Generate waveform based on instrument type
                float osc_sample = instrument_wave(phase[osc], instrument, 
                                                   wave_blend, 
                                                   brightness * filter_mod_amount,
                                                   harmonics);
                
                // Apply oscillator mixing (lower volume for sub-oscillator)
                float osc_mix = (osc == 3) ? octave_mix : (1.0f - octave_mix) / (num_oscillators - 1);
                sample += osc_sample * osc_mix;
            }
            
            // Apply envelope, amplitude and tremolo
            sample *= smooth_amp * envelope * MASTER_VOLUME * tremolo_amount;
            
            // Store in main buffer
            for (int ch = 0; ch < sfinfo.channels; ch++) {
                buffer[current_sample * sfinfo.channels + ch] = sample;
            }
            
            // Create delayed chorus signal (if used)
            if (chorus_mix > 0.0f) {
                float chorus_delay_secs = 0.02f + 0.01f * chorus_mod; // 20-30ms delay
                int chorus_delay_samples = (int)(chorus_delay_secs * sfinfo.samplerate);
                
                // Only store the chorus signal if we have enough delay space
                if (current_sample + chorus_delay_samples < sfinfo.frames) {
                    for (int ch = 0; ch < sfinfo.channels; ch++) {
                        chorus_buffer[(current_sample + chorus_delay_samples) * sfinfo.channels + ch] += 
                            sample * chorus_mix;
                    }
                }
            }
        }
        
        current_frequency = next_frequency;
    }
    
    // Mix chorus buffer into main buffer
    for (int i = 0; i < items; i++) {
        buffer[i] = buffer[i] * (1.0f - chorus_mix) + chorus_buffer[i];
    }
    
    // Apply reverb to thicken the sound (using preset's reverb_mix)
    // Temporarily override the REVERB_MIX with the preset value
    float orig_reverb_mix = g_reverb_mix;
    g_reverb_mix = reverb_mix;  // This is a bit of a hack but avoids changing the function signature
    apply_reverb(buffer, sfinfo.frames, sfinfo.channels);
    g_reverb_mix = orig_reverb_mix;  // Restore the original value
    
    // After applying reverb and before saving, apply the volume multiplier
    for (int i = 0; i < items; i++) {
        buffer[i] *= volume_multiplier;
    }
    
    // Save output
    SF_INFO outinfo = sfinfo;
    outinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    
    // Create output filename based on input, instrument, and transposition
    char output_file[256];
    
    if (custom_output_file) {
        strncpy(output_file, custom_output_file, sizeof(output_file) - 1);
        output_file[sizeof(output_file) - 1] = '\0'; // Ensure null termination
    } else {
        char *input_name = strdup(input_file);
        char *extension = strrchr(input_name, '.');
        if (extension) *extension = '\0';  // Remove extension
        
        const char *basename = strrchr(input_name, '/');
        basename = basename ? basename + 1 : input_name;  // Get filename without path
        
        // Get instrument name
        const char *instrument_names[] = {
            "pad", "pluck", "brass", "flute", "strings", 
            "organ", "bell", "bass", "wurlitzer", "acid"
        };
        
        snprintf(output_file, sizeof(output_file), "%s_%s_%.1f.wav", 
                basename, instrument_names[instrument], transpose_semitones);
        free(input_name);
    }
    
    printf("Writing output to: %s (Volume: %.2f)\n", output_file, volume_multiplier);
    
    SNDFILE *outfile = sf_open(output_file, SFM_WRITE, &outinfo);
    if (!outfile) {
        printf("Error opening output file: %s\n", sf_strerror(NULL));
        free(buffer);
        free(window_buffer);
        free(freq_data);
        free(chorus_buffer);
        return 1;
    }
    
    sf_writef_float(outfile, buffer, sfinfo.frames);
    sf_close(outfile);
    
    // Cleanup
    free(buffer);
    free(window_buffer);
    free(freq_data);
    free(chorus_buffer);
    return 0;
}

// Instrument presets
const InstrumentPreset presets[] = {
    // INSTR_PAD (0) - Lush pad sound
    {
        .num_oscillators = 4,
        .detune_amount = 0.12f,        // Increased detune for wider sound
        .attack_time = 0.8f,           // Much longer attack for slow fade-in
        .decay_time = 0.5f,            // Longer decay
        .sustain_level = 0.7f,         // Slightly lower sustain for warmth
        .release_time = 1.2f,          // Much longer release for slow fade-out
        .octave_mix = 0.4f,            // More sub-octave for fullness
        .chorus_rate = 0.12f,          // Slower chorus for smoother movement
        .chorus_depth = 0.6f,          // Deeper chorus for more richness
        .chorus_mix = 0.5f,            // More chorus for fuller sound
        .reverb_mix = 0.6f,            // More reverb for spaciousness
        .wave_blend = 0.25f,           // More sine content for roundness
        .brightness = 0.5f,            // Lower brightness to reduce harshness
        .harmonics = 0.3f,             // Fewer harmonics for smoothness
        .tremolo_rate = 0.7f,          // Slow tremolo for gentle undulation
        .tremolo_depth = 0.08f,        // Subtle tremolo depth
        .filter_mod = 0.2f             // Gentle filter modulation
    },
    
    // INSTR_PLUCK (1) - Plucked string sound
    {
        .num_oscillators = 2,
        .detune_amount = 0.01f,
        .attack_time = 0.01f,
        .decay_time = 0.3f,
        .sustain_level = 0.2f,
        .release_time = 0.1f,
        .octave_mix = 0.1f,
        .chorus_rate = 0.5f,
        .chorus_depth = 0.2f,
        .chorus_mix = 0.2f,
        .reverb_mix = 0.3f,
        .wave_blend = 0.7f,
        .brightness = 0.8f,
        .harmonics = 0.7f,
        .tremolo_rate = 0.0f,
        .tremolo_depth = 0.0f,
        .filter_mod = 0.3f
    },
    
    // INSTR_BRASS (2) - Brass sound
    {
        .num_oscillators = 2,
        .detune_amount = 0.05f,
        .attack_time = 0.1f,
        .decay_time = 0.1f,
        .sustain_level = 0.8f,
        .release_time = 0.2f,
        .octave_mix = 0.0f,
        .chorus_rate = 0.1f,
        .chorus_depth = 0.2f,
        .chorus_mix = 0.1f,
        .reverb_mix = 0.2f,
        .wave_blend = 0.8f,
        .brightness = 0.7f,
        .harmonics = 0.8f,
        .tremolo_rate = 0.0f,
        .tremolo_depth = 0.0f,
        .filter_mod = 0.2f
    },
    
    // INSTR_FLUTE (3) - Flute/wind sound
    {
        .num_oscillators = 2,
        .detune_amount = 0.03f,
        .attack_time = 0.15f,
        .decay_time = 0.1f,
        .sustain_level = 0.7f,
        .release_time = 0.15f,
        .octave_mix = 0.0f,
        .chorus_rate = 0.3f,
        .chorus_depth = 0.3f,
        .chorus_mix = 0.2f,
        .reverb_mix = 0.3f,
        .wave_blend = 0.2f,
        .brightness = 0.5f,
        .harmonics = 0.3f,
        .tremolo_rate = 5.0f,
        .tremolo_depth = 0.1f,
        .filter_mod = 0.1f
    },
    
    // INSTR_STRINGS (4) - String section
    {
        .num_oscillators = 3,
        .detune_amount = 0.1f,
        .attack_time = 0.2f,
        .decay_time = 0.1f,
        .sustain_level = 0.7f,
        .release_time = 0.3f,
        .octave_mix = 0.0f,
        .chorus_rate = 0.3f,
        .chorus_depth = 0.6f,
        .chorus_mix = 0.4f,
        .reverb_mix = 0.5f,
        .wave_blend = 0.6f,
        .brightness = 0.6f,
        .harmonics = 0.5f,
        .tremolo_rate = 5.5f,
        .tremolo_depth = 0.2f,
        .filter_mod = 0.0f
    },
    
    // INSTR_ORGAN (5) - Hammond-like organ
    {
        .num_oscillators = 3,
        .detune_amount = 0.0f,
        .attack_time = 0.01f,
        .decay_time = 0.0f,
        .sustain_level = 1.0f,
        .release_time = 0.05f,
        .octave_mix = 0.0f,
        .chorus_rate = 6.0f,
        .chorus_depth = 0.3f,
        .chorus_mix = 0.2f,
        .reverb_mix = 0.3f,
        .wave_blend = 0.9f,
        .brightness = 0.8f,
        .harmonics = 0.9f,
        .tremolo_rate = 6.0f,
        .tremolo_depth = 0.15f,
        .filter_mod = 0.0f
    },
    
    // INSTR_BELL (6) - Bell/chime sound
    {
        .num_oscillators = 2,
        .detune_amount = 0.01f,
        .attack_time = 0.01f,
        .decay_time = 0.5f,
        .sustain_level = 0.1f,
        .release_time = 0.8f,
        .octave_mix = 0.0f,
        .chorus_rate = 0.0f,
        .chorus_depth = 0.0f,
        .chorus_mix = 0.0f,
        .reverb_mix = 0.6f,
        .wave_blend = 0.8f,
        .brightness = 0.9f,
        .harmonics = 0.7f,
        .tremolo_rate = 0.0f,
        .tremolo_depth = 0.0f,
        .filter_mod = 0.0f
    },
    
    // INSTR_BASS (7) - Deep bass sound
    {
        .num_oscillators = 2,
        .detune_amount = 0.02f,
        .attack_time = 0.02f,
        .decay_time = 0.1f,
        .sustain_level = 0.8f,
        .release_time = 0.1f,
        .octave_mix = 0.0f,
        .chorus_rate = 0.0f,
        .chorus_depth = 0.0f,
        .chorus_mix = 0.0f,
        .reverb_mix = 0.1f,
        .wave_blend = 0.5f,
        .brightness = 0.4f,
        .harmonics = 0.3f,
        .tremolo_rate = 0.0f,
        .tremolo_depth = 0.0f,
        .filter_mod = 0.5f
    },
    
    // INSTR_WURLITZER (8) - Electric piano sound
    {
        .num_oscillators = 2,
        .detune_amount = 0.0f,
        .attack_time = 0.01f,
        .decay_time = 0.4f,
        .sustain_level = 0.3f,
        .release_time = 0.2f,
        .octave_mix = 0.0f,
        .chorus_rate = 0.5f,
        .chorus_depth = 0.2f,
        .chorus_mix = 0.2f,
        .reverb_mix = 0.3f,
        .wave_blend = 0.6f,
        .brightness = 0.7f,
        .harmonics = 0.5f,
        .tremolo_rate = 4.0f,
        .tremolo_depth = 0.1f,
        .filter_mod = 0.2f
    },
    
    // INSTR_ACID (9) - Acid/303-style sound
    {
        .num_oscillators = 2,         // Use 2 oscillators for more body
        .detune_amount = 0.01f,       // Very slight detune for thickness
        .attack_time = 0.01f,         // Fast attack
        .decay_time = 0.3f,
        .sustain_level = 0.7f,        // Higher sustain for more presence
        .release_time = 0.1f,         // Quick release
        .octave_mix = 0.0f,           // No sub-oscillator
        .chorus_rate = 0.0f,
        .chorus_depth = 0.0f,
        .chorus_mix = 0.0f,           // No chorus
        .reverb_mix = 0.15f,          // Just a touch of reverb
        .wave_blend = 0.7f,           // Higher value = more resonance
        .brightness = 0.9f,           // Very bright filter cutoff
        .harmonics = 0.0f,
        .tremolo_rate = 0.0f,
        .tremolo_depth = 0.0f,
        .filter_mod = 0.9f            // Strong filter modulation
    }
};