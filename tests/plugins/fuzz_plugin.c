#include "nyrexvj_plugin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct FuzzInstance {
    const NvjHost* host;
    uint32_t source;
    uint64_t state;
    uint32_t calls;
} FuzzInstance;

static uint64_t g_seed;
static int g_clean;
static char g_loose[256];
static char g_control[16];

static uint64_t mix(uint64_t* state)
{
    uint64_t z;
    *state += 0x9E3779B97F4A7C15ull;
    z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t draw(uint64_t seed, uint32_t salt)
{
    uint64_t state = seed ^ ((uint64_t)salt * 0xD1B54A32D192ED03ull);
    return mix(&state);
}

static const char* odd_text(uint64_t pick, const char* normal)
{
    switch (pick % 8u) {
    case 0:
        return NULL;
    case 1:
        return "";
    case 2:
        return g_loose;
    case 3:
        return g_control;
    default:
        return normal;
    }
}

static int32_t get_source(uint32_t index, NvjSourceInfo* out)
{
    static char ids[16][8];
    uint64_t r = draw(g_seed, 100u + index);
    uint32_t p;
    if (out == NULL) {
        return NVJ_FAILED;
    }
    if (!g_clean && (r % 20u) == 0u) {
        return NVJ_FAILED;
    }
    ids[index % 16u][0] = 's';
    ids[index % 16u][1] = (char)('a' + (int)(index % 16u));
    ids[index % 16u][2] = 0;
    out->id = ids[index % 16u];
    out->name = "Source";
    out->kind = (r >> 8) % 2u == 0u ? NVJ_SOURCE_MODULATION : NVJ_SOURCE_FRAME;
    out->parameter_count = (uint32_t)((r >> 16) % 5u);
    for (p = 0; p < NYREXVJ_PLUGIN_MAX_PARAMETERS; ++p) {
        out->parameter_names[p] = "Param";
    }
    if (!g_clean) {
        uint64_t q = draw(g_seed, 200u + index);
        if ((q % 10u) == 0u) {
            out->id = odd_text(q >> 8, out->id);
        }
        if ((q % 10u) == 1u) {
            out->name = odd_text(q >> 8, out->name);
        }
        if ((q % 10u) == 2u) {
            out->kind = (uint32_t)((q >> 8) % 6u);
        }
        if ((q % 10u) == 3u) {
            out->parameter_count = (uint32_t)((q >> 8) % 9u);
        }
        if ((q % 10u) == 4u) {
            out->id = "sa";
        }
        if ((q % 10u) == 5u) {
            out->parameter_names[(q >> 8) % NYREXVJ_PLUGIN_MAX_PARAMETERS] = odd_text(q >> 16, "Param");
        }
        if ((q % 40u) == 6u) {
            memset((char*)out + sizeof(NvjSourceInfo), 0x33, 4);
        }
    }
    return NVJ_OK;
}

static void* create(uint32_t source_index, const NvjHost* host)
{
    FuzzInstance* self;
    if (host == NULL) {
        return NULL;
    }
    if (!g_clean && (draw(g_seed, 300u + source_index) % 12u) == 0u) {
        return NULL;
    }
    self = (FuzzInstance*)calloc(1, sizeof(FuzzInstance));
    if (self == NULL) {
        return NULL;
    }
    self->host = host;
    self->source = source_index;
    self->state = g_seed + source_index;
    return self;
}

static void destroy(void* instance)
{
    free(instance);
}

static int32_t modulation(void* instance, double time, double delta, float* value)
{
    FuzzInstance* self = (FuzzInstance*)instance;
    uint64_t r;
    (void)time;
    (void)delta;
    if (self == NULL || value == NULL) {
        return NVJ_FAILED;
    }
    self->calls += 1u;
    r = mix(&self->state);
    switch ((r >> 4) % 10u) {
    case 0:
        *value = (float)NAN;
        break;
    case 1:
        *value = (float)INFINITY;
        break;
    case 2:
        *value = -(float)INFINITY;
        break;
    case 3:
        *value = 1.0e30f;
        break;
    case 4:
        *value = -1.0e30f;
        break;
    case 5:
        *value = 7.5f;
        break;
    default:
        *value = (float)((r >> 16) % 1000u) / 1000.0f;
        break;
    }
    if (self->host != NULL && NYREXVJ_HAS_FIELD(self->host, NvjHost, has_capability) && self->host->has_capability != NULL) {
        (void)self->host->has_capability(self->host->context, (r & 1u) ? "host.audio" : g_loose);
    }
    if (!g_clean && (r % 97u) == 0u) {
        return NVJ_FAILED;
    }
    return NVJ_OK;
}

static int32_t frame(void* instance, double time, double delta, NvjImage* image)
{
    FuzzInstance* self = (FuzzInstance*)instance;
    uint64_t r;
    uint32_t y;
    (void)time;
    (void)delta;
    if (self == NULL || image == NULL || image->data == NULL) {
        return NVJ_FAILED;
    }
    self->calls += 1u;
    r = mix(&self->state);
    for (y = 0; y < image->height; y += 16u) {
        memset(image->data + (size_t)y * image->stride, (int)(r & 255u), (size_t)image->stride);
    }
    if (!g_clean && (r % 53u) == 0u) {
        image->height = 3u;
    }
    if (!g_clean && (r % 59u) == 0u) {
        return NVJ_FAILED;
    }
    return NVJ_OK;
}

NYREXVJ_PLUGIN_EXPORT int32_t nyrexvj_plugin_entry(const NvjHost* host, NvjPlugin* plugin)
{
    const char* text = getenv("NVJ_FUZZ_SEED");
    uint64_t r;
    uint64_t q;
    if (host == NULL || plugin == NULL) {
        return NVJ_FAILED;
    }
    g_seed = text != NULL ? (uint64_t)strtoull(text, NULL, 10) : 1ull;
    g_clean = (g_seed % 4u) == 0u;
    memset(g_loose, 'x', sizeof(g_loose));
    memset(g_control, 'c', sizeof(g_control) - 1);
    g_control[3] = '\n';
    g_control[sizeof(g_control) - 1] = 0;
    r = draw(g_seed, 1u);
    q = draw(g_seed, 2u);
    plugin->size = sizeof(NvjPlugin);
    plugin->api_major = NYREXVJ_PLUGIN_API_MAJOR;
    plugin->api_minor = NYREXVJ_PLUGIN_API_MINOR;
    plugin->id = "fuzz";
    plugin->name = "Fuzz Plugin";
    plugin->version = "1.0.0";
    plugin->source_count = (uint32_t)(1u + (r % 8u));
    plugin->get_source = get_source;
    plugin->create = create;
    plugin->destroy = destroy;
    plugin->modulation = modulation;
    plugin->frame = frame;
    if (g_clean) {
        return NVJ_OK;
    }
    switch (q % 24u) {
    case 0:
        plugin->size = 0;
        break;
    case 1:
        plugin->size = (uint32_t)(q >> 8) % 48u;
        break;
    case 2:
        plugin->size = (uint32_t)sizeof(NvjPlugin) + 8u;
        break;
    case 3:
        plugin->api_major = (uint32_t)((q >> 8) % 4u);
        break;
    case 4:
        plugin->api_minor = (uint32_t)((q >> 8) % 4u);
        break;
    case 5:
        plugin->id = odd_text(q >> 8, "fuzz");
        break;
    case 6:
        plugin->name = odd_text(q >> 8, "Fuzz Plugin");
        break;
    case 7:
        plugin->source_count = (uint32_t)((q >> 8) % 20u);
        break;
    case 8:
        plugin->get_source = NULL;
        break;
    case 9:
        plugin->create = NULL;
        break;
    case 10:
        plugin->destroy = NULL;
        break;
    case 11:
        plugin->modulation = NULL;
        break;
    case 12:
        plugin->frame = NULL;
        break;
    case 13:
        memset((char*)plugin + sizeof(NvjPlugin), 0x44, 1u + (size_t)((q >> 8) % 8u));
        break;
    case 14:
        return NVJ_FAILED;
    default:
        break;
    }
    return NVJ_OK;
}
