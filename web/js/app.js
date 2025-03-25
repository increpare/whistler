// DOM elements
const audioUpload = document.getElementById('audio-upload');
const fileName = document.getElementById('file-name');
const playInputBtn = document.getElementById('play-input');
const playOutputBtn = document.getElementById('play-output');
const downloadBtn = document.getElementById('download-output');
const processBtn = document.getElementById('process-button');
const volumeSlider = document.getElementById('volume');
const volumeValue = document.getElementById('volume-value');
const instrumentSelect = document.getElementById('instrument-type');
const semitonesInput = document.getElementById('semitones');
const statusElement = document.getElementById('status');
const progressBar = document.getElementById('progress');

// State variables
let inputAudioBuffer = null;
let outputAudioBuffer = null;
let inputWaveSurfer = null;
let outputWaveSurfer = null;
let isProcessing = false;
let isModuleReady = false;
let Module = null;

// WebAssembly module functions
let processAudioFn = null;
let mallocFn = null;
let freeFn = null;

// Initialize wavesurfer instances
function initWaveSurfers() {
    inputWaveSurfer = WaveSurfer.create({
        container: '#input-waveform',
        waveColor: '#6200ea',
        progressColor: '#9d46ff',
        cursorColor: 'transparent',
        height: 120,
        responsive: true,
        normalize: true,
        barWidth: 2,
        barGap: 1
    });

    outputWaveSurfer = WaveSurfer.create({
        container: '#output-waveform',
        waveColor: '#0a00b6',
        progressColor: '#6200ea',
        cursorColor: 'transparent',
        height: 120,
        responsive: true,
        normalize: true,
        barWidth: 2,
        barGap: 1
    });

    // Handle playback events
    inputWaveSurfer.on('finish', () => {
        playInputBtn.textContent = 'Play Input';
    });

    outputWaveSurfer.on('finish', () => {
        playOutputBtn.textContent = 'Play Output';
    });
}

// Initialize the application
function init() {
    initWaveSurfers();
    setupEventListeners();
    updateStatus('Loading WebAssembly module...');
    processBtn.disabled = true;
    
    // The WhistlerModule function should be available from the whistler.js script
    if (typeof WhistlerModule === 'undefined') {
        updateStatus('Error: WhistlerModule not found. Please check the script loading.');
        return;
    }
    
    // Initialize the module
    const moduleConfig = {
        print: function(text) { console.log('stdout:', text); },
        printErr: function(text) { console.error('stderr:', text); },
        onRuntimeInitialized: function() {
            console.log('Runtime initialized');
        }
    };
    
    // Create the module instance
    WhistlerModule(moduleConfig).then(function(instance) {
        Module = instance;
        console.log('Module instance created');
        
        // Now that we have the module instance, we can initialize the functions
        try {
            // Create wrapper functions
            processAudioFn = Module.cwrap('process_audio', 'number', 
                ['number', 'number', 'number', 'number', 'number', 'number']);
            mallocFn = Module._malloc;
            freeFn = Module._free;
            
            isModuleReady = true;
            updateStatus('WebAssembly module loaded. Upload an audio file to begin.');
            
            // Enable process button if we have audio loaded
            if (inputAudioBuffer) {
                processBtn.disabled = false;
            }
        } catch (error) {
            console.error('Error initializing WebAssembly functions:', error);
            updateStatus('Error initializing WebAssembly functions. Please refresh the page.');
        }
    }).catch(function(error) {
        console.error('Error creating module instance:', error);
        updateStatus('Error loading WebAssembly module. Please refresh the page.');
    });
}

// Set up event listeners
function setupEventListeners() {
    // File upload
    audioUpload.addEventListener('change', handleFileUpload);
    
    // Play buttons
    playInputBtn.addEventListener('click', () => {
        if (inputWaveSurfer.isPlaying()) {
            inputWaveSurfer.pause();
            playInputBtn.textContent = 'Play Input';
        } else {
            inputWaveSurfer.play();
            playInputBtn.textContent = 'Pause';
        }
    });
    
    playOutputBtn.addEventListener('click', () => {
        if (outputWaveSurfer.isPlaying()) {
            outputWaveSurfer.pause();
            playOutputBtn.textContent = 'Play Output';
        } else {
            outputWaveSurfer.play();
            playOutputBtn.textContent = 'Pause';
        }
    });
    
    // Process button
    processBtn.addEventListener('click', processAudio);
    
    // Volume slider
    volumeSlider.addEventListener('input', () => {
        volumeValue.textContent = `${volumeSlider.value}%`;
    });
    
    // Download button
    downloadBtn.addEventListener('click', downloadOutput);
}

// Handle file upload
async function handleFileUpload(event) {
    const file = event.target.files[0];
    if (!file) return;
    
    fileName.textContent = file.name;
    updateStatus('Loading audio file...');
    setProgress(10);
    
    try {
        const arrayBuffer = await file.arrayBuffer();
        const audioContext = new (window.AudioContext || window.webkitAudioContext)();
        
        // Decode the audio file
        audioContext.decodeAudioData(arrayBuffer, (buffer) => {
            inputAudioBuffer = buffer;
            
            // Load into wavesurfer
            const blob = new Blob([arrayBuffer], { type: file.type });
            inputWaveSurfer.loadBlob(blob);
            
            playInputBtn.disabled = false;
            processBtn.disabled = !isModuleReady;
            updateStatus('Audio loaded. Ready to process.');
            setProgress(0);
        }, (error) => {
            console.error('Error decoding audio file:', error);
            updateStatus('Error loading audio. Please try another file.');
            setProgress(0);
        });
    } catch (error) {
        console.error('Error loading file:', error);
        updateStatus('Error loading file. Please try again.');
        setProgress(0);
    }
}

// Process audio using WebAssembly module
async function processAudio() {
    if (!inputAudioBuffer || isProcessing || !isModuleReady) {
        if (!isModuleReady) {
            updateStatus('WebAssembly module not ready yet. Please wait...');
        }
        return;
    }
    
    isProcessing = true;
    updateStatus('Processing audio...');
    setProgress(30);
    
    const instrumentType = parseInt(instrumentSelect.value, 10);
    const semitones = parseInt(semitonesInput.value, 10);
    const volume = volumeSlider.value / 100;
    
    try {
        // Convert AudioBuffer to float array
        const inputData = inputAudioBuffer.getChannelData(0);
        
        // Allocate memory in WebAssembly module
        const inputPtr = mallocFn(inputData.length * 4);
        const outputLengthPtr = mallocFn(4);
        
        // Copy input data to WebAssembly memory
        Module.HEAPF32.set(inputData, inputPtr / 4);
        
        setProgress(50);
        updateStatus('Applying effects...');
        
        // Call the WebAssembly function
        const outputPtr = processAudioFn(
            inputPtr, 
            inputData.length, 
            instrumentType, 
            semitones, 
            volume, 
            outputLengthPtr
        );
        
        setProgress(80);
        
        // Get the output length
        const outputLength = Module.getValue(outputLengthPtr, 'i32');
        
        // Read the output data
        const outputData = new Float32Array(outputLength);
        outputData.set(Module.HEAPF32.subarray(outputPtr / 4, outputPtr / 4 + outputLength));
        
        // Create a new AudioBuffer with the result
        const audioContext = new (window.AudioContext || window.webkitAudioContext)();
        outputAudioBuffer = audioContext.createBuffer(1, outputLength, inputAudioBuffer.sampleRate);
        outputAudioBuffer.getChannelData(0).set(outputData);
        
        // Convert to a blob for wavesurfer
        const blob = audioBufferToWaveBlob(outputAudioBuffer);
        outputWaveSurfer.loadBlob(blob);
        
        // Enable output controls
        playOutputBtn.disabled = false;
        downloadBtn.disabled = false;
        
        updateStatus('Processing complete!');
        setProgress(100);
        
        // Clean up allocated memory
        freeFn(inputPtr);
        freeFn(outputLengthPtr);
        freeFn(outputPtr);
    } catch (error) {
        console.error('Error processing audio:', error);
        updateStatus('Error processing audio. Please try again.');
        setProgress(0);
    } finally {
        isProcessing = false;
    }
}

// Convert AudioBuffer to WAV Blob
function audioBufferToWaveBlob(audioBuffer) {
    const numberOfChannels = audioBuffer.numberOfChannels;
    const sampleRate = audioBuffer.sampleRate;
    const length = audioBuffer.length;
    
    // Create the WAV file
    const buffer = new ArrayBuffer(44 + length * numberOfChannels * 2);
    const view = new DataView(buffer);
    
    // Write RIFF header
    writeString(view, 0, 'RIFF');
    view.setUint32(4, 36 + length * numberOfChannels * 2, true);
    writeString(view, 8, 'WAVE');
    
    // Write fmt chunk
    writeString(view, 12, 'fmt ');
    view.setUint32(16, 16, true);
    view.setUint16(20, 1, true); // PCM format
    view.setUint16(22, numberOfChannels, true);
    view.setUint32(24, sampleRate, true);
    view.setUint32(28, sampleRate * numberOfChannels * 2, true); // Byte rate
    view.setUint16(32, numberOfChannels * 2, true); // Block align
    view.setUint16(34, 16, true); // Bits per sample
    
    // Write data chunk
    writeString(view, 36, 'data');
    view.setUint32(40, length * numberOfChannels * 2, true);
    
    // Write audio data
    const offset = 44;
    for (let i = 0; i < length; i++) {
        for (let channel = 0; channel < numberOfChannels; channel++) {
            const sample = Math.max(-1, Math.min(1, audioBuffer.getChannelData(channel)[i]));
            view.setInt16(offset + (i * numberOfChannels + channel) * 2, sample * 0x7FFF, true);
        }
    }
    
    return new Blob([buffer], { type: 'audio/wav' });
}

// Helper function to write a string to a DataView
function writeString(view, offset, string) {
    for (let i = 0; i < string.length; i++) {
        view.setUint8(offset + i, string.charCodeAt(i));
    }
}

// Download the processed audio file
function downloadOutput() {
    if (!outputAudioBuffer) return;
    
    const blob = audioBufferToWaveBlob(outputAudioBuffer);
    const url = URL.createObjectURL(blob);
    
    const a = document.createElement('a');
    a.href = url;
    a.download = 'whistler_output.wav';
    a.click();
    
    URL.revokeObjectURL(url);
}

// Update status message
function updateStatus(message) {
    statusElement.textContent = message;
}

// Set progress bar value
function setProgress(value) {
    progressBar.style.width = `${value}%`;
    if (value === 0 || value === 100) {
        setTimeout(() => {
            progressBar.style.width = '0';
        }, 1000);
    }
}

// Initialize the application when the page loads
document.addEventListener('DOMContentLoaded', init); 