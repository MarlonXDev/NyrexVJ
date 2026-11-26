#pragma once

#include <cstdint>
#include <string>

namespace nvj {
namespace ndi {

struct SendCreate {
    const char* name;
    const char* groups;
    bool clockVideo;
    bool clockAudio;
};

struct VideoFrame {
    int xres;
    int yres;
    uint32_t fourCC;
    int rateN;
    int rateD;
    float aspect;
    int formatType;
    int64_t timecode;
    const uint8_t* data;
    int stride;
    const char* metadata;
    int64_t timestamp;
};

struct Source {
    const char* name;
    const char* url;
};

struct FindCreate {
    bool showLocalSources;
    const char* groups;
    const char* extraIps;
};

struct RecvCreate {
    Source source;
    int colorFormat;
    int bandwidth;
    bool allowVideoFields;
    const char* receiverName;
};

static_assert(sizeof(void*) != 8 || sizeof(VideoFrame) == 72, "NDI video frame layout");
static_assert(sizeof(void*) != 8 || sizeof(SendCreate) == 24, "NDI send create layout");
static_assert(sizeof(void*) != 8 || sizeof(Source) == 16, "NDI source layout");
static_assert(sizeof(void*) != 8 || sizeof(FindCreate) == 24, "NDI find create layout");
static_assert(sizeof(void*) != 8 || sizeof(RecvCreate) == 40, "NDI receiver create layout");

constexpr uint32_t fourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<unsigned char>(a)) | (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr uint32_t kFourCCRgbx = fourCC('R', 'G', 'B', 'X');
constexpr uint32_t kFourCCRgba = fourCC('R', 'G', 'B', 'A');
constexpr uint32_t kFourCCBgrx = fourCC('B', 'G', 'R', 'X');
constexpr uint32_t kFourCCBgra = fourCC('B', 'G', 'R', 'A');
constexpr uint32_t kFourCCUyvy = fourCC('U', 'Y', 'V', 'Y');
constexpr uint32_t kFourCCUyva = fourCC('U', 'Y', 'V', 'A');

constexpr int kFormatProgressive = 1;
constexpr int64_t kTimecodeSynthesize = INT64_MAX;
constexpr int64_t kTimestampUndefined = INT64_MIN;

constexpr int kColorUyvyRgba = 3;
constexpr int kBandwidthHighest = 100;

constexpr int kFrameNone = 0;
constexpr int kFrameVideo = 1;
constexpr int kFrameError = 4;

using InitializeFn = bool (*)();
using DestroyFn = void (*)();
using SendCreateFn = void* (*)(const SendCreate*);
using SendDestroyFn = void (*)(void*);
using SendVideoFn = void (*)(void*, const VideoFrame*);
using VersionFn = const char* (*)();
using ConnectionsFn = int (*)(void*, uint32_t);
using FindCreateFn = void* (*)(const FindCreate*);
using FindDestroyFn = void (*)(void*);
using FindWaitFn = bool (*)(void*, uint32_t);
using FindSourcesFn = const Source* (*)(void*, uint32_t*);
using RecvCreateFn = void* (*)(const RecvCreate*);
using RecvDestroyFn = void (*)(void*);
using RecvCaptureFn = int (*)(void*, VideoFrame*, void*, void*, uint32_t);
using RecvFreeVideoFn = void (*)(void*, VideoFrame*);
using RecvConnectionsFn = int (*)(void*);

struct Api {
    VersionFn version = nullptr;
    InitializeFn initialize = nullptr;
    DestroyFn destroy = nullptr;
    SendCreateFn sendCreate = nullptr;
    SendDestroyFn sendDestroy = nullptr;
    SendVideoFn sendVideo = nullptr;
    ConnectionsFn sendConnections = nullptr;
    FindCreateFn findCreate = nullptr;
    FindDestroyFn findDestroy = nullptr;
    FindWaitFn findWait = nullptr;
    FindSourcesFn findSources = nullptr;
    RecvCreateFn recvCreate = nullptr;
    RecvDestroyFn recvDestroy = nullptr;
    RecvCaptureFn recvCapture = nullptr;
    RecvFreeVideoFn recvFreeVideo = nullptr;
    RecvConnectionsFn recvConnections = nullptr;

    bool canSend() const { return sendCreate && sendDestroy && sendVideo; }
    bool canReceive() const { return findCreate && findDestroy && findWait && findSources && recvCreate && recvDestroy && recvCapture && recvFreeVideo; }
};

struct RuntimeInfo {
    std::string library;
    std::string version;
    bool captureV3 = false;
};

std::string sanitizeVersion(const char* text);
bool available();
void recheck();
std::string loadedLibrary();
RuntimeInfo runtimeInfo();
std::string loadError();
void setLibraryOverride(const std::string& path);
bool load(Api& out);
bool loadForSending(Api& out, std::string& error);
bool loadForReceiving(Api& out, std::string& error);

}
}
