#include "nyrexvj_plugin.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define ADD(counter, amount) InterlockedExchangeAdd(&(counter), (amount))
#define READ(counter) InterlockedCompareExchange(&(counter), 0, 0)
#else
#include <time.h>
#define ADD(counter, amount) __sync_fetch_and_add(&(counter), (amount))
#define READ(counter) __sync_fetch_and_add(&(counter), 0)
#endif

#define SOURCE_ENTRIES 0
#define SOURCE_OVERLAP 1
#define SOURCE_DESTROYS 2
#define SOURCE_LIVE 3
#define SOURCE_CAP_KNOWN 4
#define SOURCE_CAP_UNKNOWN 5
#define SOURCE_LIMIT 6
#define SOURCE_FAIL_TEXT 7
#define SOURCE_COUNT 8

#if defined(_WIN32)
static volatile LONG g_entries;
static volatile LONG g_overlap;
static volatile LONG g_destroys;
static volatile LONG g_creates;
static volatile LONG g_in_destroy;
#else
static volatile long g_entries;
static volatile long g_overlap;
static volatile long g_destroys;
static volatile long g_creates;
static volatile long g_in_destroy;
#endif

typedef struct ProbeInstance {
    const NvjHost* host;
    uint32_t source;
    uint32_t calls;
} ProbeInstance;

static void pause_ms(unsigned int ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec span;
    span.tv_sec = (time_t)(ms / 1000u);
    span.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&span, NULL);
#endif
}

static unsigned int env_ms(const char* name)
{
    const char* text = getenv(name);
    if (text == NULL || text[0] == '\0') {
        return 0u;
    }
    return (unsigned int)atoi(text);
}

static const char* const kNames[SOURCE_COUNT] = {"entries", "overlap", "destroys", "live", "cap_known", "cap_unknown", "limit", "fail_text"};

static int32_t get_source(uint32_t index, NvjSourceInfo* out)
{
    if (out == NULL || index >= SOURCE_COUNT) {
        return NVJ_FAILED;
    }
    out->id = kNames[index];
    out->name = kNames[index];
    out->kind = NVJ_SOURCE_MODULATION;
    out->parameter_count = 0;
    return NVJ_OK;
}

static void* create(uint32_t source_index, const NvjHost* host)
{
    ProbeInstance* self;
    if (READ(g_in_destroy) > 0) {
        ADD(g_overlap, 1);
    }
    if (host == NULL || source_index >= SOURCE_COUNT) {
        return NULL;
    }
    self = (ProbeInstance*)calloc(1, sizeof(ProbeInstance));
    if (self == NULL) {
        return NULL;
    }
    self->host = host;
    self->source = source_index;
    ADD(g_creates, 1);
    return self;
}

static void destroy(void* instance)
{
    unsigned int delay = env_ms("NVJ_PROBE_DESTROY_MS");
    ADD(g_in_destroy, 1);
    if (delay > 0u) {
        pause_ms(delay);
    }
    free(instance);
    ADD(g_destroys, 1);
    ADD(g_in_destroy, -1);
}

static float scaled(long value, float divisor)
{
    float result = (float)value / divisor;
    return result > 1.0f ? 1.0f : (result < 0.0f ? 0.0f : result);
}

static int32_t modulation(void* instance, double time, double delta, float* value)
{
    ProbeInstance* self = (ProbeInstance*)instance;
    (void)time;
    (void)delta;
    if (self == NULL || value == NULL) {
        return NVJ_FAILED;
    }
    self->calls += 1u;
    switch (self->source) {
    case SOURCE_ENTRIES:
        *value = scaled((long)READ(g_entries), 16.0f);
        return NVJ_OK;
    case SOURCE_OVERLAP:
        *value = scaled((long)READ(g_overlap), 4.0f);
        return NVJ_OK;
    case SOURCE_DESTROYS:
        *value = scaled((long)READ(g_destroys), 64.0f);
        return NVJ_OK;
    case SOURCE_LIVE:
        *value = scaled((long)READ(g_creates) - (long)READ(g_destroys), 16.0f);
        return NVJ_OK;
#if NYREXVJ_PLUGIN_API_MINOR >= 1
    case SOURCE_CAP_KNOWN:
    case SOURCE_CAP_UNKNOWN: {
        const char* name = self->source == SOURCE_CAP_KNOWN ? NYREXVJ_CAP_SOURCE_MODULATION : "vendor.unknown";
        if (!NYREXVJ_HAS_FIELD(self->host, NvjHost, has_capability) || self->host->has_capability == NULL) {
            *value = 0.25f;
        } else {
            *value = self->host->has_capability(self->host->context, name) == 1 ? 1.0f : 0.0f;
        }
        return NVJ_OK;
    }
    case SOURCE_LIMIT: {
        NvjLimits limits;
        memset(&limits, 0, sizeof(limits));
        limits.size = sizeof(limits);
        if (!NYREXVJ_HAS_FIELD(self->host, NvjHost, get_limits) || self->host->get_limits == NULL || self->host->get_limits(self->host->context, &limits) != NVJ_OK) {
            *value = 0.25f;
        } else {
            *value = scaled((long)limits.frame_width, 4096.0f);
        }
        return NVJ_OK;
    }
    case SOURCE_FAIL_TEXT:
        if (self->calls >= 3u) {
            if (NYREXVJ_HAS_FIELD(self->host, NvjHost, set_error) && self->host->set_error != NULL) {
                const char* longText = getenv("NVJ_PROBE_LONG_TEXT");
                if (longText != NULL && longText[0] == '1') {
                    char text[700];
                    size_t i;
                    for (i = 0; i < sizeof(text) - 1u; ++i) {
                        text[i] = (i % 10u == 5u) ? '\n' : (char)('a' + (int)(i % 26u));
                    }
                    text[sizeof(text) - 1u] = '\0';
                    self->host->set_error(self->host->context, text);
                } else {
                    self->host->set_error(self->host->context, "the probe refuses to go on");
                }
            }
            return NVJ_FAILED;
        }
        *value = 0.5f;
        return NVJ_OK;
#else
    case SOURCE_CAP_KNOWN:
    case SOURCE_CAP_UNKNOWN:
    case SOURCE_LIMIT:
        *value = 0.25f;
        return NVJ_OK;
    case SOURCE_FAIL_TEXT:
        if (self->calls >= 3u) {
            return NVJ_FAILED;
        }
        *value = 0.5f;
        return NVJ_OK;
#endif
    default:
        return NVJ_FAILED;
    }
}

NYREXVJ_PLUGIN_EXPORT int32_t nyrexvj_plugin_entry(const NvjHost* host, NvjPlugin* plugin)
{
    unsigned int delay;
    if (host == NULL || plugin == NULL) {
        return NVJ_FAILED;
    }
    if (READ(g_in_destroy) > 0) {
        ADD(g_overlap, 1);
    }
    ADD(g_entries, 1);
    delay = env_ms("NVJ_PROBE_LOAD_MS");
    if (delay > 0u) {
        pause_ms(delay);
    }
    plugin->size = sizeof(NvjPlugin);
    plugin->api_major = NYREXVJ_PLUGIN_API_MAJOR;
    plugin->api_minor = NYREXVJ_PLUGIN_API_MINOR;
    plugin->id = "probe";
    plugin->name = "Probe Plugin";
    plugin->version = "1.0.0";
    plugin->source_count = SOURCE_COUNT;
    plugin->get_source = get_source;
    plugin->create = create;
    plugin->destroy = destroy;
    plugin->modulation = modulation;
    plugin->frame = NULL;
    return NVJ_OK;
}
