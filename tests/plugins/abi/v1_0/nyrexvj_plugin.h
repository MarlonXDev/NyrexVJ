#ifndef NYREXVJ_PLUGIN_H
#define NYREXVJ_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NYREXVJ_PLUGIN_API_MAJOR 1u
#define NYREXVJ_PLUGIN_API_MINOR 0u

#define NYREXVJ_PLUGIN_ENTRY_NAME "nyrexvj_plugin_entry"

#define NYREXVJ_PLUGIN_MAX_SOURCES 8u
#define NYREXVJ_PLUGIN_MAX_PARAMETERS 4u
#define NYREXVJ_PLUGIN_MAX_WIDTH 1920u
#define NYREXVJ_PLUGIN_MAX_HEIGHT 1080u

#if defined(_WIN32)
#define NYREXVJ_PLUGIN_EXPORT __declspec(dllexport)
#else
#define NYREXVJ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define NYREXVJ_HAS_FIELD(pointer, type, field) \
    ((pointer) != NULL && (size_t)(pointer)->size >= offsetof(type, field) + sizeof(((type*)0)->field))

typedef enum NvjLogLevel {
    NVJ_LOG_INFO = 0,
    NVJ_LOG_WARNING = 1,
    NVJ_LOG_ERROR = 2
} NvjLogLevel;

typedef enum NvjSourceKind {
    NVJ_SOURCE_MODULATION = 1,
    NVJ_SOURCE_FRAME = 2
} NvjSourceKind;

typedef enum NvjResult {
    NVJ_OK = 0,
    NVJ_FAILED = 1
} NvjResult;

typedef struct NvjAudio {
    uint32_t size;
    float bass;
    float mid;
    float high;
    float level;
    float kick;
} NvjAudio;

typedef struct NvjBeat {
    uint32_t size;
    double beat;
    float bpm;
    uint32_t beats_per_bar;
} NvjBeat;

typedef struct NvjHost {
    uint32_t size;
    uint32_t api_major;
    uint32_t api_minor;
    void* context;
    void (*log)(void* context, int32_t level, const char* message);
    int32_t (*get_audio)(void* context, NvjAudio* out);
    int32_t (*get_beat)(void* context, NvjBeat* out);
    float (*get_parameter)(void* context, uint32_t index);
} NvjHost;

typedef struct NvjSourceInfo {
    uint32_t size;
    const char* id;
    const char* name;
    uint32_t kind;
    uint32_t parameter_count;
    const char* parameter_names[NYREXVJ_PLUGIN_MAX_PARAMETERS];
} NvjSourceInfo;

typedef struct NvjImage {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t* data;
} NvjImage;

typedef struct NvjPlugin {
    uint32_t size;
    uint32_t api_major;
    uint32_t api_minor;
    const char* id;
    const char* name;
    const char* version;
    uint32_t source_count;
    int32_t (*get_source)(uint32_t index, NvjSourceInfo* out);
    void* (*create)(uint32_t source_index, const NvjHost* host);
    void (*destroy)(void* instance);
    int32_t (*modulation)(void* instance, double time, double delta, float* value);
    int32_t (*frame)(void* instance, double time, double delta, NvjImage* image);
} NvjPlugin;

typedef int32_t (*NvjEntryFn)(const NvjHost* host, NvjPlugin* plugin);

#ifdef __cplusplus
}
#endif

#endif
