//
// Created by user on 03-09-2025.
//
#include <stddef.h>
#ifndef VOIP_OFFSETS_H
#define VOIP_OFFSETS_H

#define EMULATOR true

#ifdef EMULATOR

// Offsets for Emulator api 34 - Android 14 - rooted with rootAVD
#define AUDIOFLINGER_SETMODE_OFFSET 0x4DE40
// android::AudioFlinger::setMode(audio_mode_t)	000000000004DE40
#define RECORDTRACK_GETNEXTBUFFER_OFFSET 0x1364F0
// android::AudioFlinger::RecordThread::RecordTrack::getNextBuffer(android::AudioBufferProvider::Buffer *)	00000000001364F0
#define TRACK_GETNEXTBUFFER_OFFSET 0x12C2A0
// android::AudioFlinger::PlaybackThread::Track::getNextBuffer(android::AudioBufferProvider::Buffer *)	000000000012C2A0
#define TRACK_STOP_OFFSET 0x12D7D0
// android::AudioFlinger::PlaybackThread::Track::stop(void)	000000000012D7D0
#define RECORDTRACK_STOP_OFFSET 0x1367C0
// android::AudioFlinger::RecordThread::RecordTrack::stop(void)	00000000001367C0

// Offsets from your reversed analysis
constexpr size_t SAMPLERATE_OFFSET = 0x178;
constexpr size_t UID_OFFSET = 0x234;
constexpr size_t ISOUT_OFFSET = 0x1C8;
constexpr size_t FORMAT_OFFSET = 0x17C;
constexpr size_t CHANNELMASK_OFFSET = 0x180;
constexpr size_t ATTR_OFFSET = 0x68;
// Offset End


#else

// Offsets for Samsung S22 - Android 14 - S901EXXSCEYB1 - rooted with Magisk

#define AUDIOFLINGER_SETMODE_OFFSET 0x749E4
// android::AudioFlinger::setMode(audio_mode_t)	00000000000749E4
#define RECORDTRACK_GETNEXTBUFFER_OFFSET 0x18728C
// android::AudioFlinger::RecordThread::RecordTrack::getNextBuffer(android::AudioBufferProvider::Buffer *)	000000000018728C
#define TRACK_GETNEXTBUFFER_OFFSET 0x177FB0
// android::AudioFlinger::PlaybackThread::Track::getNextBuffer(android::AudioBufferProvider::Buffer *)	0000000000177FB0
#define TRACK_STOP_OFFSET 0x1799AC
// android::AudioFlinger::PlaybackThread::Track::stop(void)	00000000001799AC
#define RECORDTRACK_STOP_OFFSET 0x1876D4
// android::AudioFlinger::RecordThread::RecordTrack::stop(void)	00000000001876D4

// Offsets from your reversed analysis
constexpr size_t SAMPLERATE_OFFSET = 0x178;
constexpr size_t UID_OFFSET = 0x234;
constexpr size_t ISOUT_OFFSET = 0x1C8;
constexpr size_t FORMAT_OFFSET = 0x17C;
constexpr size_t CHANNELMASK_OFFSET = 0x180;
constexpr size_t ATTR_OFFSET = 0x68;
// Offset End


#endif


// Symbol names (mangled)

// const char* sym_RecordTrack_getNextBuffer =
//     "_ZN7android12AudioFlinger12RecordThread11RecordTrack13getNextBufferEPNS_19AudioBufferProvider6BufferE";
// const char* sym_Track_getNextBuffer =
//     "_ZN7android12AudioFlinger14PlaybackThread5Track13getNextBufferEPNS_19AudioBufferProvider6BufferE";
// const char* sym_Track_stop =
//     "_ZN7android12AudioFlinger14PlaybackThread5Track4stopEv";
// const char* sym_RecordTrack_stop =
//     "_ZN7android12AudioFlinger12RecordThread11RecordTrack4stopEv";


#endif //VOIP_OFFSETS_H
