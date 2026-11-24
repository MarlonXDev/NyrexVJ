#ifndef NVJ_ABI_ENTRY_H
#define NVJ_ABI_ENTRY_H

#include <stddef.h>
#include <stdint.h>

typedef struct AbiEntry {
    const char* name;
    uint64_t offset;
    uint64_t size;
} AbiEntry;

#define ABI_FIELD(type, field) {#type "." #field, (uint64_t)offsetof(type, field), (uint64_t)sizeof(((type*)0)->field)}
#define ABI_SIZE(type) {#type " sizeof", (uint64_t)sizeof(type), 0}
#define ABI_CONST(name) {"const " #name, (uint64_t)(name), 0}

#define ABI_COMMON_TABLE \
    ABI_SIZE(NvjAudio), ABI_FIELD(NvjAudio, size), ABI_FIELD(NvjAudio, bass), ABI_FIELD(NvjAudio, mid), ABI_FIELD(NvjAudio, high), ABI_FIELD(NvjAudio, level), ABI_FIELD(NvjAudio, kick), \
    ABI_SIZE(NvjBeat), ABI_FIELD(NvjBeat, size), ABI_FIELD(NvjBeat, beat), ABI_FIELD(NvjBeat, bpm), ABI_FIELD(NvjBeat, beats_per_bar), \
    ABI_FIELD(NvjHost, size), ABI_FIELD(NvjHost, api_major), ABI_FIELD(NvjHost, api_minor), ABI_FIELD(NvjHost, context), ABI_FIELD(NvjHost, log), ABI_FIELD(NvjHost, get_audio), ABI_FIELD(NvjHost, get_beat), ABI_FIELD(NvjHost, get_parameter), \
    ABI_SIZE(NvjSourceInfo), ABI_FIELD(NvjSourceInfo, size), ABI_FIELD(NvjSourceInfo, id), ABI_FIELD(NvjSourceInfo, name), ABI_FIELD(NvjSourceInfo, kind), ABI_FIELD(NvjSourceInfo, parameter_count), ABI_FIELD(NvjSourceInfo, parameter_names), \
    ABI_SIZE(NvjImage), ABI_FIELD(NvjImage, size), ABI_FIELD(NvjImage, width), ABI_FIELD(NvjImage, height), ABI_FIELD(NvjImage, stride), ABI_FIELD(NvjImage, data), \
    ABI_SIZE(NvjPlugin), ABI_FIELD(NvjPlugin, size), ABI_FIELD(NvjPlugin, api_major), ABI_FIELD(NvjPlugin, api_minor), ABI_FIELD(NvjPlugin, id), ABI_FIELD(NvjPlugin, name), ABI_FIELD(NvjPlugin, version), ABI_FIELD(NvjPlugin, source_count), \
    ABI_FIELD(NvjPlugin, get_source), ABI_FIELD(NvjPlugin, create), ABI_FIELD(NvjPlugin, destroy), ABI_FIELD(NvjPlugin, modulation), ABI_FIELD(NvjPlugin, frame), \
    ABI_CONST(NYREXVJ_PLUGIN_API_MAJOR), ABI_CONST(NYREXVJ_PLUGIN_MAX_SOURCES), ABI_CONST(NYREXVJ_PLUGIN_MAX_PARAMETERS), ABI_CONST(NYREXVJ_PLUGIN_MAX_WIDTH), ABI_CONST(NYREXVJ_PLUGIN_MAX_HEIGHT), \
    ABI_CONST(NVJ_LOG_INFO), ABI_CONST(NVJ_LOG_WARNING), ABI_CONST(NVJ_LOG_ERROR), ABI_CONST(NVJ_SOURCE_MODULATION), ABI_CONST(NVJ_SOURCE_FRAME), ABI_CONST(NVJ_OK), ABI_CONST(NVJ_FAILED)

#endif
