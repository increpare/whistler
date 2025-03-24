# Whistler

Whistler is an audio synthesis and processing tool (made largely with the help of cursor, so: caveat emptor) that allows you to transform raw audio into quirky synthesized sounds and to overlay them. It consists of two main components:

1. **whistler** - A tool that transforms audio files into synthetic instruments with various effects
2. **chorus** - A multi-track audio mixer that creates compositions from multiple processed audio files

## Features

- **Audio Transformation**: Convert any monophonic audio (like whistling, humming, or singing) into synthesized instruments
- **Multiple Instruments**: Choose from 10 different instrument types including pads, plucks, strings, brass, and more
- **Audio Effects**: Apply chorus, reverb, and other effects to create rich soundscapes
- **Multi-Track Mixing**: Create complex compositions by layering multiple processed tracks

## Requirements

To build and run Whistler, you'll need:

- C compiler (gcc or clang)
- libsndfile (audio file handling)
- FFTW3 (Fast Fourier Transform library)
- json-c (JSON parsing for the chorus tool)
- sox (for the final mixing stage)

## Installation

### macOS (using Homebrew)

```bash
# Install dependencies
brew install libsndfile fftw json-c sox

# Clone the repository
git clone https://github.com/yourusername/whistler.git
cd whistler

# Build the tools
make
```

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install libsndfile1-dev libfftw3-dev libjson-c-dev sox

# Fedora/RHEL
sudo dnf install libsndfile-devel fftw-devel json-c-devel sox

# Clone the repository
git clone https://github.com/yourusername/whistler.git
cd whistler

# Build the tools
make
```

## Usage

### Whistler (Audio Transformation)

The `whistler` tool takes an input audio file and transforms it into a synthesized instrument.

```bash
./whistler <input_wav_file> [semitones] [instrument] [volume] [output_file]
```

Parameters:
- `input_wav_file`: Path to the source WAV file (monophonic audio works best)
- `semitones`: Transposition amount (positive or negative)
- `instrument`: Instrument type (0-9 or name)
  - 0/pad: Lush Pad
  - 1/pluck: Plucked String
  - 2/brass: Brass
  - 3/flute: Flute
  - 4/strings: Strings
  - 5/organ: Organ
  - 6/bell: Bell
  - 7/bass: Bass
  - 8/wurlitzer: Wurlitzer
  - 9/acid: Acid
- `volume`: Output volume multiplier (0.0-10.0, default: 1.0)
- `output_file`: Path to the output WAV file (optional)

Example:
```bash
./whistler samples/test.wav -12 strings 1.2 output/my_strings.wav
```

### Chorus (Multi-Track Mixer)

The `chorus` tool combines multiple processed audio files into a composition based on a JSON configuration file.

```bash
./chorus <json_file>
```

The JSON file should have the following format:
```json
{
    "song_name": "MySong",
    "tracks": [
        {
            "file": "input1.wav",
            "instrument": "pad",
            "transpose": 0,
            "volume": 1
        },
        {
            "file": "input2.wav",
            "instrument": "strings",
            "transpose": -12,
            "volume": 1
        }
    ]
}
```

All source files should be placed in the `samples/` directory. The final composition will be saved to `output/<song_name>.wav`.

Example:
```bash
./chorus chori/song1.json
```

## Project Structure

- `src/`: Source code
- `samples/`: Input audio files
- `intermediate/`: Temporary processed files
- `output/`: Final output files
- `chori/`: JSON configuration files for compositions

## Examples

The project includes sample WAV files and JSON configurations:

1. Process a single file:
```bash
./whistler samples/test.wav -12 pad 1.0 output/test_pad.wav
```

2. Create a multi-track composition:
```bash
./chorus chori/song1.json
```

## How It Works

1. The `whistler` tool:
   - Analyzes the input audio to extract frequency and amplitude data
   - Applies transposition to the detected frequencies
   - Synthesizes new audio using the selected instrument type
   - Adds effects like chorus and reverb

2. The `chorus` tool:
   - Reads a JSON configuration file
   - Processes each track using the `whistler` program
   - Resamples all tracks to a common sample rate
   - Mixes them together to create the final composition

## License

do wha'ever ye want.
