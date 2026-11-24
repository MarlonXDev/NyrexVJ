#ifndef NYREXVJ_PLUGIN_H
#define NYREXVJ_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NYREXVJ_PLUGIN_API_MAJOR 1u
#define NYREXVJ_PLUGIN_API_MINOR 1u

#define NYREXVJ_PLUGIN_ENTRY_NAME "nyrexvj_plugin_entry"

#define NYREXVJ_PLUGIN_MAX_SOURCES 8u
#define NYREXVJ_PLUGIN_MAX_PARAMETERS 4u
#define NYREXVJ_PLUGIN_MAX_WIDTH 1920u
#define NYREXVJ_PLUGIN_MAX_HEIGHT 1080u

#define NYREXVJ_CAP_SOURCE_MODULATION "source.modulation"
#define NYREXVJ_CAP_SOURCE_FRAME "source.frame"
#define NYREXVJ_CAP_FRAME_RGBA8 "frame.rgba8"
#define NYREXVJ_CAP_HOST_LOG "host.log"
#define NYREXVJ_CAP_HOST_AUDIO "host.audio"
#define NYREXVJ_CAP_HOST_BEAT "host.beat"
#define NYREXVJ_CAP_HOST_PARAMETERS "host.parameters"
#define NYREXVJ_CAP_HOST_LIMITS "host.limits"
#define NYREXVJ_CAP_HOST_SET_ERROR "host.set_error"

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

typedef struct NvjLimits {
    uint32_t size;
    uint32_t max_sources;
    uint32_t max_parameters;
    uint32_t frame_width;
    uint32_t frame_height;
    uint32_t frame_rate;
    uint32_t modulation_rate;
    uint32_t modulation_budget_ms;
    uint32_t frame_budget_ms;
    uint32_t create_budget_ms;
    uint32_t load_budget_ms;
    uint32_t max_instances;
    uint32_t log_lines_per_second;
    uint32_t log_line_length;
} NvjLimits;

typedef struct NvjHost {
    uint32_t size;
    uint32_t api_major;
    uint32_t api_minor;
    void* context;
    void (*log)(void* context, int32_t level, const char* message);
    int32_t (*get_audio)(void* context, NvjAudio* out);
    int32_t (*get_beat)(void* context, NvjBeat* out);
    float (*get_parameter)(void* context, uint32_t index);
    int32_t (*has_capability)(void* context, const char* name);
    int32_t (*get_limits)(void* context, NvjLimits* out);
    void (*set_error)(void* context, const char* message);
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
