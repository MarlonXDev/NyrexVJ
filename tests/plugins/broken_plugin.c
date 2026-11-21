#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nyrexvj_plugin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#define MODE_SLOW 1
#define MODE_HANG 2
#define MODE_FAIL 3
#define MODE_CRASH 4
#define MODE_RANGE 5
#define MODE_CREATE_FAIL 6
#define MODE_ENTRY_FAIL 7
#define MODE_NO_ENTRY 8
#define MODE_DUPLICATE_SOURCE 9
#define MODE_API_MISMATCH 10
#define MODE_LOAD_SLOW 11
#define MODE_OVERFLOW 12
#define MODE_IMAGE_CHANGED 13
#define MODE_SLOW_FRAME 14
#define MODE_TOO_MANY_SOURCES 15
#define MODE_ID_MISMATCH 16
#define MODE_NO_MODULATION 17
#define MODE_CRASH_CREATE 18
#define MODE_CRASH_LOAD 19
#define MODE_LOG_SPAM 20

#ifndef NVJ_BROKEN_MODE
#error NVJ_BROKEN_MODE must be defined
#endif

typedef struct BrokenInstance {
    const NvjHost* host;
    uint32_t source;
    uint32_t calls;
} BrokenInstance;

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

static void crash_now(void)
{
    volatile int* target = (volatile int*)0;
    *target = 1;
}

static int32_t get_source(uint32_t index, NvjSourceInfo* out)
{
    if (out == NULL) {
        return NVJ_FAILED;
    }
    if (index == 0) {
        out->id = "mod";
        out->name = "Broken modulation";
        out->kind = NVJ_SOURCE_MODULATION;
        out->parameter_count = 0;
        return NVJ_OK;
    }
    if (index == 1) {
#if NVJ_BROKEN_MODE == MODE_DUPLICATE_SOURCE
        out->id = "mod";
#else
        out->id = "img";
#endif
        out->name = "Broken image";
        out->kind = NVJ_SOURCE_FRAME;
        out->parameter_count = 0;
        return NVJ_OK;
    }
#if NVJ_BROKEN_MODE == MODE_TOO_MANY_SOURCES
    if (index < 9) {
        static char ids[9][8];
        ids[index][0] = 's';
        ids[index][1] = (char)('0' + index);
        ids[index][2] = 0;
        out->id = ids[index];
        out->name = "Extra";
        out->kind = NVJ_SOURCE_MODULATION;
        out->parameter_count = 0;
        return NVJ_OK;
    }
#endif
    return NVJ_FAILED;
}

static void* create(uint32_t source_index, const NvjHost* host)
{
#if NVJ_BROKEN_MODE == MODE_CREATE_FAIL
    (void)source_index;
    (void)host;
    return NULL;
#else
    BrokenInstance* self;
#if NVJ_BROKEN_MODE == MODE_CRASH_CREATE
    crash_now();
#endif
    self = (BrokenInstance*)calloc(1, sizeof(BrokenInstance));
    if (self == NULL) {
        return NULL;
    }
    self->host = host;
    self->source = source_index;
    return self;
#endif
}

static void destroy(void* instance)
{
    free(instance);
}

static int32_t modulation(void* instance, double time, double delta, float* value)
{
    BrokenInstance* self = (BrokenInstance*)instance;
    (void)time;
    (void)delta;
    if (self == NULL || value == NULL) {
        return NVJ_FAILED;
    }
    self->calls += 1u;
#if NVJ_BROKEN_MODE == MODE_SLOW
    pause_ms(60u);
    *value = 0.5f;
    return NVJ_OK;
#elif NVJ_BROKEN_MODE == MODE_HANG
    for (;;) {
        pause_ms(10u);
    }
#elif NVJ_BROKEN_MODE == MODE_FAIL
    *value = 0.5f;
    return NVJ_FAILED;
#elif NVJ_BROKEN_MODE == MODE_CRASH
    if (self->calls > 2u) {
        crash_now();
    }
    *value = 0.5f;
    return NVJ_OK;
#elif NVJ_BROKEN_MODE == MODE_RANGE
    if (self->calls == 1u) {
        *value = (float)NAN;
    } else if (self->calls == 2u) {
        *value = 5.0f;
    } else if (self->calls == 3u) {
        *value = -3.0f;
    } else if (self->calls == 4u) {
        *value = (float)INFINITY;
    } else {
        *value = 0.25f;
    }
    return NVJ_OK;
#elif NVJ_BROKEN_MODE == MODE_LOG_SPAM
    if (self->host != NULL && self->host->log != NULL) {
        int i;
        for (i = 0; i < 50; ++i) {
            self->host->log(self->host->context, NVJ_LOG_INFO, "spam");
        }
    }
    *value = 0.5f;
    return NVJ_OK;
#else
    *value = 0.5f;
    return NVJ_OK;
#endif
}

static int32_t frame(void* instance, double time, double delta, NvjImage* image)
{
    BrokenInstance* self = (BrokenInstance*)instance;
    (void)time;
    (void)delta;
    if (self == NULL || image == NULL || image->data == NULL) {
        return NVJ_FAILED;
    }
    self->calls += 1u;
#if NVJ_BROKEN_MODE == MODE_OVERFLOW
    memset(image->data, 0x40, (size_t)image->stride * image->height + 16u);
    return NVJ_OK;
#elif NVJ_BROKEN_MODE == MODE_IMAGE_CHANGED
    memset(image->data, 0x40, (size_t)image->stride * image->height);
    image->width = 10u;
    return NVJ_OK;
#elif NVJ_BROKEN_MODE == MODE_SLOW_FRAME
    pause_ms(250u);
    memset(image->data, 0x40, (size_t)image->stride * image->height);
    return NVJ_OK;
#else
    memset(image->data, 0x40, (size_t)image->stride * image->height);
    return NVJ_OK;
#endif
}

#if NVJ_BROKEN_MODE == MODE_NO_ENTRY
#define ENTRY_NAME nyrexvj_plugin_other_name
#else
#define ENTRY_NAME nyrexvj_plugin_entry
#endif

NYREXVJ_PLUGIN_EXPORT int32_t ENTRY_NAME(const NvjHost* host, NvjPlugin* plugin)
{
    if (host == NULL || plugin == NULL) {
        return NVJ_FAILED;
    }
#if NVJ_BROKEN_MODE == MODE_ENTRY_FAIL
    return NVJ_FAILED;
#endif
#if NVJ_BROKEN_MODE == MODE_CRASH_LOAD
    crash_now();
#endif
#if NVJ_BROKEN_MODE == MODE_LOAD_SLOW
    pause_ms(2500u);
#endif
#if NVJ_BROKEN_MODE == MODE_API_MISMATCH
    plugin->api_major = 2u;
    plugin->api_minor = 0u;
#else
    plugin->api_major = NYREXVJ_PLUGIN_API_MAJOR;
    plugin->api_minor = NYREXVJ_PLUGIN_API_MINOR;
#endif
#if NVJ_BROKEN_MODE == MODE_ID_MISMATCH
    plugin->id = "someone-else";
#else
    plugin->id = "broken";
#endif
    plugin->name = "Broken Plugin";
    plugin->version = "0.0.1";
#if NVJ_BROKEN_MODE == MODE_TOO_MANY_SOURCES
    plugin->source_count = 9;
#else
    plugin->source_count = 2;
#endif
    plugin->get_source = get_source;
    plugin->create = create;
    plugin->destroy = destroy;
#if NVJ_BROKEN_MODE == MODE_NO_MODULATION
    plugin->modulation = NULL;
#else
    plugin->modulation = modulation;
#endif
    plugin->frame = frame;
    return NVJ_OK;
}
