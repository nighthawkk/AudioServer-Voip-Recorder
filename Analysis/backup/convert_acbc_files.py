#!/usr/bin/env python3
# convert_bc_files.py - Convert .bc files to WAV with correct 16kHz headers

import os
import sys
import wave
import struct
import glob
import json
from datetime import datetime

def convert_bc_to_wav(bc_file, sample_rate=16000, channels=1, sample_width=2):
    """
    Convert .bc (uplink) file to WAV with proper headers
    Default: 16kHz mono 16-bit (Android standard for voice)
    """
    wav_file = bc_file.replace('.bc', '.wav')
    
    print(f"Converting: {os.path.basename(bc_file)}")
    print(f"  Config: {sample_rate}Hz, {channels}ch, {sample_width*8}-bit")
    
    with open(bc_file, 'rb') as f:
        pcm_data = f.read()
    
    # Calculate duration
    total_frames = len(pcm_data) // (channels * sample_width)
    duration = total_frames / sample_rate
    print(f"  Duration: {duration:.2f} seconds")
    
    # Write WAV with proper headers
    with wave.open(wav_file, 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(sample_width)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm_data)
    
    print(f"  Output: {os.path.basename(wav_file)}\n")
    return wav_file

def convert_ac_to_wav(ac_file, sample_rate=8000, channels=2, sample_width=2):
    """
    Convert .ac (downlink) file to WAV
    Default: 48kHz stereo 16-bit (typical for speaker output)
    """
    wav_file = ac_file.replace('.ac', '.wav')
    
    print(f"Converting: {os.path.basename(ac_file)}")
    print(f"  Config: {sample_rate}Hz, {channels}ch, {sample_width*8}-bit")
    
    with open(ac_file, 'rb') as f:
        pcm_data = f.read()
    
    # Calculate duration
    total_frames = len(pcm_data) // (channels * sample_width)
    duration = total_frames / sample_rate
    print(f"  Duration: {duration:.2f} seconds")
    
    # Write WAV
    with wave.open(wav_file, 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(sample_width)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm_data)
    
    print(f"  Output: {os.path.basename(wav_file)}\n")
    return wav_file

def save_metadata(session_dir):
    """Save conversion metadata for reference"""
    metadata = {
        "conversion_time": datetime.now().isoformat(),
        "uplink_config": {
            "sample_rate": 16000,
            "channels": 1,
            "bit_depth": 16,
            "description": "Android microphone capture (WhatsApp)"
        },
        "downlink_config": {
            "sample_rate": 8000,
            "channels": 2,
            "bit_depth": 16,
            "description": "Android speaker output"
        }
    }
    
    meta_file = os.path.join(session_dir, "audio_metadata.json")
    with open(meta_file, 'w') as f:
        json.dump(metadata, f, indent=2)
    print(f"Saved metadata to: {meta_file}\n")

def batch_convert(session_dir):
    """Convert all .bc and .ac files in a session directory"""
    
    print(f"\n{'='*60}")
    print(f"Audio Session Converter")
    print(f"{'='*60}\n")
    
    if not os.path.isdir(session_dir):
        print(f"Error: Directory not found: {session_dir}")
        return
    
    # Find all audio files
    bc_files = glob.glob(os.path.join(session_dir, "*.bc"))
    ac_files = glob.glob(os.path.join(session_dir, "*.ac"))
    
    print(f"Found {len(bc_files)} uplink (.bc) files")
    print(f"Found {len(ac_files)} downlink (.ac) files\n")
    
    # Convert uplink files (16kHz mono)
    if bc_files:
        print("Converting UPLINK (microphone) files...")
        print("-" * 40)
        for bc_file in bc_files:
            convert_bc_to_wav(bc_file, sample_rate=16000, channels=1)
    
    # Convert downlink files (48kHz stereo)
    if ac_files:
        print("Converting DOWNLINK (speaker) files...")
        print("-" * 40)
        for ac_file in ac_files:
            # First check file size to guess if mono or stereo
            file_size = os.path.getsize(ac_file)
            
            # Try both mono and stereo for .ac files
            # Stereo is more common for speaker output
            if file_size % 4 == 0:  # Could be stereo
                convert_ac_to_wav(ac_file, sample_rate=8000, channels=2)
            else:  # Likely mono
                convert_ac_to_wav(ac_file, sample_rate=16000, channels=1)
    
    # Save metadata
    save_metadata(session_dir)
    
    print(f"{'='*60}")
    print(f"Conversion complete!")
    print(f"All WAV files saved in: {session_dir}")
    print(f"{'='*60}\n")

def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_bc_files.py <session_directory>")
        print("       python convert_bc_files.py <single_file.bc>")
        sys.exit(1)
    
    path = sys.argv[1]
    
    if os.path.isdir(path):
        # Convert entire session directory
        batch_convert(path)
    elif path.endswith('.bc'):
        # Convert single BC file with correct 16kHz setting
        convert_bc_to_wav(path, sample_rate=16000, channels=1, sample_width=2)
    elif path.endswith('.ac'):
        # Convert single AC file
        convert_ac_to_wav(path, sample_rate=8000, channels=2, sample_width=2)
    else:
        print(f"Error: Unknown file type: {path}")
        sys.exit(1)

if __name__ == "__main__":
    main()