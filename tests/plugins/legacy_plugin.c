#include "nyrexvj_plugin.h"

#include <stdlib.h>
#include <string.h>

typedef struct LegacyInstance {
    const NvjHost* host;
    uint32_t source;
} LegacyInstance;

static float parameter(const LegacyInstance* self)
{
    if (!NYREXVJ_HAS_FIELD(self->host, NvjHost, get_parameter) || self->host->get_parameter == NULL) {
        return 0.0f;
    }
    return self->host->get_parameter(self->host->context, 0u);
}

static float audio_level(const LegacyInstance* self)
{
    NvjAudio audio;
    if (!NYREXVJ_HAS_FIELD(self->host, NvjHost, get_audio) || self->host->get_audio == NULL) {
        return 0.0f;
    }
    memset(&audio, 0, sizeof(audio));
    audio.size = sizeof(audio);
    if (self->host->get_audio(self->host->context, &audio) != NVJ_OK) {
        return 0.0f;
    }
    return audio.level;
}

static int32_t get_source(uint32_t index, NvjSourceInfo* out)
{
    if (out == NULL || out->size < sizeof(NvjSourceInfo)) {
        return NVJ_FAILED;
    }
    if (index == 0) {
        out->id = "level";
        out->name = "Level";
        out->kind = NVJ_SOURCE_MODULATION;
        out->parameter_count = 1;
        out->parameter_names[0] = "Mix";
        return NVJ_OK;
    }
    if (index == 1) {
        out->id = "tone";
        out->name = "Tone";
        out->kind = NVJ_SOURCE_FRAME;
        out->parameter_count = 1;
        out->parameter_names[0] = "Red";
        return NVJ_OK;
    }
    return NVJ_FAILED;
}

static void* create(uint32_t source_index, const NvjHost* host)
{
    LegacyInstance* self;
    if (host == NULL || source_index > 1) {
        return NULL;
    }
    self = (LegacyInstance*)calloc(1, sizeof(LegacyInstance));
    if (self == NULL) {
        return NULL;
    }
    self->host = host;
    self->source = source_index;
    return self;
}

static void destroy(void* instance)
{
    free(instance);
}

static int32_t modulation(void* instance, double time, double delta, float* value)
{
    LegacyInstance* self = (LegacyInstance*)instance;
    float mix;
    (void)time;
    (void)delta;
    if (self == NULL || value == NULL || self->source != 0) {
        return NVJ_FAILED;
    }
    mix = parameter(self);
    *value = mix * 0.5f + audio_level(self) * 0.5f;
    return NVJ_OK;
}

static int32_t frame(void* instance, double time, double delta, NvjImage* image)
{
    LegacyInstance* self = (LegacyInstance*)instance;
    uint32_t x;
    uint32_t y;
    uint8_t red;
    (void)delta;
    if (self == NULL || self->source != 1 || image == NULL || image->data == NULL || image->stride < image->width * 4u) {
        return NVJ_FAILED;
    }
    red = (uint8_t)(parameter(self) * 255.0f);
    for (y = 0; y < image->height; ++y) {
        uint8_t* row = image->data + (size_t)y * image->stride;
        for (x = 0; x < image->width; ++x) {
            row[x * 4u + 0u] = red;
            row[x * 4u + 1u] = (uint8_t)((x + (uint32_t)(time * 60.0)) & 255u);
            row[x * 4u + 2u] = (uint8_t)(y & 255u);
            row[x * 4u + 3u] = 255u;
        }
    }
    return NVJ_OK;
}

NYREXVJ_PLUGIN_EXPORT int32_t nyrexvj_plugin_entry(const NvjHost* host, NvjPlugin* plugin)
{
    if (host == NULL || plugin == NULL || host->api_major != NYREXVJ_PLUGIN_API_MAJOR) {
        return NVJ_FAILED;
    }
    plugin->size = sizeof(NvjPlugin);
    plugin->api_major = NYREXVJ_PLUGIN_API_MAJOR;
    plugin->api_minor = NYREXVJ_PLUGIN_API_MINOR;
    plugin->id = "nyrexvj-legacy";
    plugin->name = "Legacy 1.0 Plugin";
    plugin->version = "1.0.0";
    plugin->source_count = 2;
    plugin->get_source = get_source;
    plugin->create = create;
    plugin->destroy = destroy;
    plugin->modulation = modulation;
    plugin->frame = frame;
    return NVJ_OK;
}
