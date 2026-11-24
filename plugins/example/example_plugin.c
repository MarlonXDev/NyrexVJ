#include "nyrexvj_plugin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PALETTE_SIZE 1024
#define TWO_PI 6.28318530717958647692

typedef struct ExampleInstance {
    const NvjHost* host;
    uint32_t source;
    float palette[PALETTE_SIZE][3];
    float* columns;
    float* rows;
    float* diagonals;
    uint32_t width;
    uint32_t height;
} ExampleInstance;

static float parameter(const ExampleInstance* self, uint32_t index)
{
    if (!NYREXVJ_HAS_FIELD(self->host, NvjHost, get_parameter) || self->host->get_parameter == NULL) {
        return 0.5f;
    }
    return self->host->get_parameter(self->host->context, index);
}

static void report(const ExampleInstance* self, const char* text)
{
    if (NYREXVJ_HAS_FIELD(self->host, NvjHost, set_error) && self->host->set_error != NULL) {
        self->host->set_error(self->host->context, text);
    }
}

static float kick(const ExampleInstance* self)
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
    return audio.kick;
}

static int32_t get_source(uint32_t index, NvjSourceInfo* out)
{
    if (out == NULL || out->size < sizeof(NvjSourceInfo)) {
        return NVJ_FAILED;
    }
    if (index == 0) {
        out->id = "pulse";
        out->name = "Pulse";
        out->kind = NVJ_SOURCE_MODULATION;
        out->parameter_count = 2;
        out->parameter_names[0] = "Rate";
        out->parameter_names[1] = "Kick mix";
        return NVJ_OK;
    }
    if (index == 1) {
        out->id = "plasma";
        out->name = "Plasma";
        out->kind = NVJ_SOURCE_FRAME;
        out->parameter_count = 3;
        out->parameter_names[0] = "Speed";
        out->parameter_names[1] = "Color shift";
        out->parameter_names[2] = "Zoom";
        return NVJ_OK;
    }
    return NVJ_FAILED;
}

static void* create(uint32_t source_index, const NvjHost* host)
{
    ExampleInstance* self;
    if (host == NULL || source_index > 1) {
        return NULL;
    }
    self = (ExampleInstance*)calloc(1, sizeof(ExampleInstance));
    if (self == NULL) {
        return NULL;
    }
    self->host = host;
    self->source = source_index;
    return self;
}

static void destroy(void* instance)
{
    ExampleInstance* self = (ExampleInstance*)instance;
    if (self == NULL) {
        return;
    }
    free(self->columns);
    free(self->rows);
    free(self->diagonals);
    free(self);
}

static int32_t modulation(void* instance, double time, double delta, float* value)
{
    ExampleInstance* self = (ExampleInstance*)instance;
    double rate;
    double wave;
    double mix;
    (void)delta;
    if (self == NULL || value == NULL || self->source != 0) {
        return NVJ_FAILED;
    }
    rate = 0.25 + 4.0 * (double)parameter(self, 0);
    wave = 0.5 + 0.5 * sin(TWO_PI * rate * time);
    mix = (double)parameter(self, 1);
    *value = (float)(wave * (1.0 - mix) + (double)kick(self) * mix);
    return NVJ_OK;
}

static int prepare(ExampleInstance* self, uint32_t width, uint32_t height)
{
    float* columns;
    float* rows;
    float* diagonals;
    if (self->width == width && self->height == height) {
        return 1;
    }
    columns = (float*)malloc(sizeof(float) * width);
    rows = (float*)malloc(sizeof(float) * height);
    diagonals = (float*)malloc(sizeof(float) * ((size_t)width + height));
    if (columns == NULL || rows == NULL || diagonals == NULL) {
        free(columns);
        free(rows);
        free(diagonals);
        return 0;
    }
    free(self->columns);
    free(self->rows);
    free(self->diagonals);
    self->columns = columns;
    self->rows = rows;
    self->diagonals = diagonals;
    self->width = width;
    self->height = height;
    return 1;
}

static int32_t frame(void* instance, double time, double delta, NvjImage* image)
{
    ExampleInstance* self = (ExampleInstance*)instance;
    double speed;
    double shift;
    double zoom;
    double bright;
    uint32_t x;
    uint32_t y;
    uint32_t i;
    (void)delta;
    if (self == NULL || self->source != 1 || image == NULL || image->data == NULL) {
        return NVJ_FAILED;
    }
    if (image->width == 0 || image->height == 0 || image->width > NYREXVJ_PLUGIN_MAX_WIDTH || image->height > NYREXVJ_PLUGIN_MAX_HEIGHT || image->stride < image->width * 4u) {
        report(self, "the image size is not supported");
        return NVJ_FAILED;
    }
    if (!prepare(self, image->width, image->height)) {
        report(self, "not enough memory for the lookup tables");
        return NVJ_FAILED;
    }
    speed = 0.2 + 2.0 * (double)parameter(self, 0);
    shift = (double)parameter(self, 1);
    zoom = 0.5 + 3.0 * (double)parameter(self, 2);
    bright = 0.75 + 0.25 * (double)kick(self);
    for (i = 0; i < PALETTE_SIZE; ++i) {
        double phase = (double)i / (double)(PALETTE_SIZE - 1) + shift;
        self->palette[i][0] = (float)((0.5 + 0.5 * cos(TWO_PI * phase)) * bright);
        self->palette[i][1] = (float)((0.5 + 0.5 * cos(TWO_PI * (phase + 0.33))) * bright);
        self->palette[i][2] = (float)((0.5 + 0.5 * cos(TWO_PI * (phase + 0.67))) * bright);
    }
    for (x = 0; x < image->width; ++x) {
        self->columns[x] = (float)sin((double)x * 0.02 * zoom + time * speed);
    }
    for (y = 0; y < image->height; ++y) {
        self->rows[y] = (float)sin((double)y * 0.03 * zoom - time * speed * 1.3);
    }
    for (i = 0; i < image->width + image->height; ++i) {
        self->diagonals[i] = (float)sin((double)i * 0.015 * zoom + time * speed * 0.7);
    }
    for (y = 0; y < image->height; ++y) {
        uint8_t* row = image->data + (size_t)y * image->stride;
        for (x = 0; x < image->width; ++x) {
            float sum = self->columns[x] + self->rows[y] + self->diagonals[x + y];
            int index = (int)((sum / 6.0f + 0.5f) * (float)(PALETTE_SIZE - 1));
            if (index < 0) {
                index = 0;
            }
            if (index > PALETTE_SIZE - 1) {
                index = PALETTE_SIZE - 1;
            }
            row[x * 4u + 0u] = (uint8_t)(self->palette[index][0] * 255.0f);
            row[x * 4u + 1u] = (uint8_t)(self->palette[index][1] * 255.0f);
            row[x * 4u + 2u] = (uint8_t)(self->palette[index][2] * 255.0f);
            row[x * 4u + 3u] = 255u;
        }
    }
    return NVJ_OK;
}

NYREXVJ_PLUGIN_EXPORT int32_t nyrexvj_plugin_entry(const NvjHost* host, NvjPlugin* plugin)
{
    if (host == NULL || plugin == NULL || host->api_major != NYREXVJ_PLUGIN_API_MAJOR || plugin->size < sizeof(NvjPlugin)) {
        return NVJ_FAILED;
    }
    plugin->size = sizeof(NvjPlugin);
    plugin->api_major = NYREXVJ_PLUGIN_API_MAJOR;
    plugin->api_minor = NYREXVJ_PLUGIN_API_MINOR;
    plugin->id = "nyrexvj-example";
    plugin->name = "Example Plugin";
    plugin->version = "1.0.0";
    plugin->source_count = 2;
    plugin->get_source = get_source;
    plugin->create = create;
    plugin->destroy = destroy;
    plugin->modulation = modulation;
    plugin->frame = frame;
    return NVJ_OK;
}
