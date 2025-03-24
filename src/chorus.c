/*

This file loads up a .json file given in the arguments. 

The json file looks like:

{
    "song_name": "BeautifulTrio",
    "tracks":[
        {
            "file": "othat.wav",
            "instrument": "acid",
            "transpose": 0,
            "volume": 1
        },
        {
            "file": "test.wav",
            "instrument": "strings",
            "transpose": -12,
            "volume": 1
        },
        {
            "file": "glissandotest.wav",
            "instrument": "pad",
            "transpose": -5,
            "volume": 1
        }    
    ]    
}

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <json_file>\n", argv[0]);
        return 1;
    }

    const char *json_file = argv[1];

    // Open the JSON file
    FILE *file = fopen(json_file, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", json_file);
        return 1;
    }

    // Read the file into a string
    fseek(file, 0, SEEK_END);   
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *json_data = (char *)malloc(file_size + 1);
    if (!json_data) {
        fprintf(stderr, "Error: Could not allocate memory for JSON data\n");    
        fclose(file);
        return 1;
    }

    fread(json_data, 1, file_size, file);
    json_data[file_size] = '\0';    
    fclose(file);

    // Parse the JSON data
    json_object *root = json_tokener_parse(json_data);
    if (!root) {
        fprintf(stderr, "Error: Could not parse JSON data\n");
        free(json_data);
        return 1;
    }   

    // Extract overall song name
    json_object *song_name = json_object_object_get(root, "song_name");
    if (!json_object_is_type(song_name, json_type_string)) {
        fprintf(stderr, "Error: 'song_name' is not a string\n");
        json_object_put(root);
        free(json_data);
        return 1;
    }
    const char *song_name_str = json_object_get_string(song_name);

    // Extract the "tracks" array
    json_object *tracks = json_object_object_get(root, "tracks");
    if (!json_object_is_type(tracks, json_type_array)) {
        fprintf(stderr, "Error: 'tracks' is not an array\n");
        json_object_put(root);  
        free(json_data);
        return 1;
    }

    // Get the number of tracks
    int num_tracks = json_object_array_length(tracks);
    printf("Number of tracks: %d\n", num_tracks);

    //step 1, delete all files in the intermediate directory
    char command[256];
    snprintf(command, sizeof(command), "rm -f intermediate/*.wav");
    printf("Executing: %s\n", command);
    int result = system(command);
    if (result != 0) {
        fprintf(stderr, "Error: Command failed with return code %d\n", result);
        json_object_put(root);
        free(json_data);
        return 1;
    }

    // Iterate through the tracks - generating the songs by calling
    // the whistler program
    for (int i = 0; i < num_tracks; i++) {
        json_object *track = json_object_array_get_idx(tracks, i);
        if (!json_object_is_type(track, json_type_object)) {
            fprintf(stderr, "Error: Track %d is not an object\n", i);
            json_object_put(root);
            free(json_data);
            return 1;
        }

        // Extract the filename, instrument, and volume
        json_object *filename = json_object_object_get(track, "file");
        json_object *instrument = json_object_object_get(track, "instrument");
        json_object *transpose = json_object_object_get(track, "transpose");
        json_object *volume = json_object_object_get(track, "volume");
        
        if (!json_object_is_type(filename, json_type_string) ||
            !json_object_is_type(instrument, json_type_string) ||
            !json_object_is_type(transpose, json_type_int) ||
            !json_object_is_type(volume, json_type_int)) {
            fprintf(stderr, "Error: Invalid track format\n");
            json_object_put(root);
            free(json_data);    
            return 1;
        }

        // Get the values as strings
        const char *filename_str = json_object_get_string(filename);
        const char *instrument_str = json_object_get_string(instrument);    
        int transpose_int = json_object_get_int(transpose);
        int volume_int = json_object_get_int(volume);

        // Construct the command line arguments
        char command[256];
        // Whistler commands look like:
        // Usage: ./whistler <input_wav_file> [semitones] [instrument] [volume] [output_file]
        //input wav file is in "samples" directory
        char input_file[256];
        snprintf(input_file, sizeof(input_file), "samples/%s", filename_str);
        //output wav is in "intermediate" directory - you can use i as the file name
        char output_file[256];
        snprintf(output_file, sizeof(output_file), "intermediate/%d.wav", i);

        //call the whistler program
        snprintf(command, sizeof(command), "./whistler %s %d %s %d %s", input_file, transpose_int, instrument_str, volume_int, output_file);
        printf("Executing: %s\n", command);
        int result = system(command);
        if (result != 0) {
            fprintf(stderr, "Error: Command failed with return code %d\n", result);
        }
    }

    // now that we've generated intermediate/0...(n-1).wav files, use sox to mix them together 
    // and output to output/<song_name>.wav
    char output_file[256];
    snprintf(output_file, sizeof(output_file), "output/%s.wav", song_name_str);
    
    // First, resample all files to 44100 Hz
    for (int i = 0; i < num_tracks; i++) {
        char input_file[256];
        char resampled_file[256];
        snprintf(input_file, sizeof(input_file), "intermediate/%d.wav", i);
        snprintf(resampled_file, sizeof(resampled_file), "intermediate/%d_resampled.wav", i);
        
        snprintf(command, sizeof(command), "sox %s %s rate 44100 reverb 40 50 40 echo 0.8 0.9 1000.0 0.3", input_file, resampled_file);
        printf("Executing: %s\n", command);
        result = system(command);
        if (result != 0) {
            fprintf(stderr, "Error: Command failed with return code %d\n", result);
        }
    }
    
    // Then mix all resampled files
    snprintf(command, sizeof(command), "sox -m intermediate/*_resampled.wav %s", output_file);
    printf("Executing: %s\n", command);
    result = system(command);
    if (result != 0) {
        fprintf(stderr, "Error: Command failed with return code %d\n", result);
    }
 
    json_object_put(root);
    free(json_data);

    return 0;
}

        
        
        
