#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

constexpr int kMaxOutputs = 4;
constexpr int kMinWarpPoints = 2;
constexpr int kMaxWarpPoints = 17;
constexpr int kMeshDivisions = 32;
constexpr int kMaxMeshDivisions = 128;
constexpr int kMaxSlices = 8;
constexpr int kMinMaskPoints = 3;
constexpr int kMaxMaskPoints = 32;
constexpr int kMaskWidth = 1024;
constexpr int kMaskHeight = 576;
constexpr float kMaxMaskFeather = 0.25f;
constexpr int kBezierSegments = 16;
constexpr size_t kMaxMaskImageNameLength = 120;
constexpr size_t kOutputGpuBudgetBytes = 384u * 1024u * 1024u;
constexpr int kMaxNdiFrameSize = 4096;

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
    bool operator==(const Vec2f&) const = default;
};

struct EdgeBlend {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float power = 2.0f;
    float gamma = 2.2f;
    float blackLevel = 0.0f;
    bool operator==(const EdgeBlend&) const = default;
};

struct MaskHandle {
    Vec2f in;
    Vec2f out;
    bool operator==(const MaskHandle&) const = default;
};

struct SliceMask {
    bool enabled = false;
    std::vector<Vec2f> points;
    float feather = 0.0f;
    bool invert = false;
    std::vector<MaskHandle> handles;
    std::string image;
    bool operator==(const SliceMask&) const = default;
};

struct MaskBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> coverage;
};

struct OutputColor {
    float contrast = 1.0f;
    float saturation = 1.0f;
    float gamma = 1.0f;
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float black = 0.0f;
    bool operator==(const OutputColor&) const = default;
};

struct OutputMapping {
    std::array<float, 4> slice{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<Vec2f, 4> corners{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    int warpColumns = 2;
    int warpRows = 2;
    std::vector<Vec2f> warp = std::vector<Vec2f>(4);
    EdgeBlend blend;
    float brightness = 1.0f;
    bool flipX = false;
    bool flipY = false;
    bool fill = false;
    SliceMask mask;
    OutputColor sliceColor;
    bool operator==(const OutputMapping&) const = default;
};

struct OutputSlice {
    std::string name;
    bool enabled = true;
    OutputMapping map;
    bool operator==(const OutputSlice&) const = default;
};

struct OutputSpec {
    std::string name;
    bool enabled = false;
    int monitor = 0;
    bool fullscreen = false;
    int delayFrames = 0;
    OutputMapping map;
    OutputColor color;
    std::vector<OutputSlice> slices;
    bool ndi = false;
    std::string ndiName;
    bool operator==(const OutputSpec&) const = default;
};

struct MeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

void normalizeMapping(OutputMapping& map);
void normalizeMask(SliceMask& mask);
void normalizeColor(OutputColor& color);
void normalizeSpec(OutputSpec& spec);
bool isDefaultColor(const OutputColor& color);
void applyColor(const OutputColor& color, float rgb[3]);
int meshDivisionsFor(const OutputMapping& map);
int sliceCount(const OutputSpec& spec);
const OutputMapping& sliceMapping(const OutputSpec& spec, int index);
OutputMapping& sliceMapping(OutputSpec& spec, int index);
std::string sliceName(const OutputSpec& spec, int index);
bool addSlice(OutputSpec& spec);
bool removeSlice(OutputSpec& spec, int index);
bool duplicateSlice(OutputSpec& spec, int index);
void resizeWarp(OutputMapping& map, int columns, int rows);
bool isDefaultMapping(const OutputMapping& map);
void resetCorners(OutputMapping& map);
void resetWarp(OutputMapping& map);

Vec2f warpOffsetAt(const OutputMapping& map, float s, float t);
Vec2f mapPoint(const OutputMapping& map, float s, float t);
std::vector<MeshVertex> buildMesh(const OutputMapping& map, int divisions = kMeshDivisions);
std::vector<uint32_t> buildMeshIndices(int divisions = kMeshDivisions);

float edgeRamp(float t, float power, float gamma);
float edgeFactor(const EdgeBlend& blend, float x, float y);
bool hasEdgeBlend(const EdgeBlend& blend);
float blendZone(const EdgeBlend& blend, float x, float y);
float blackLift(const EdgeBlend& blend, float x, float y);

bool maskUsable(const SliceMask& mask);
bool maskIsImage(const SliceMask& mask);
bool maskHasCurves(const SliceMask& mask);
bool maskImageNameValid(const std::string& name);
std::vector<Vec2f> maskOutline(const SliceMask& mask, int segments = kBezierSegments);
void maskSmooth(SliceMask& mask, float tension);
void maskClearCurves(SliceMask& mask);
std::vector<Vec2f> simplifyStroke(const std::vector<Vec2f>& stroke, int maxPoints);
bool maskFromStroke(SliceMask& mask, const std::vector<Vec2f>& stroke);
bool decodeMaskBitmap(const uint8_t* data, size_t size, MaskBitmap& out, std::string& error);
SliceMask maskRectangle(float left, float top, float right, float bottom);
SliceMask maskEllipse(float centerX, float centerY, float radiusX, float radiusY, int segments = 32);
bool maskContains(const SliceMask& mask, float x, float y);
int maskInsertPoint(SliceMask& mask);
bool maskRemovePoint(SliceMask& mask, int index);
std::vector<uint8_t> buildMaskImage(const SliceMask& mask, int width = kMaskWidth, int height = kMaskHeight, const MaskBitmap* bitmap = nullptr);

void layoutGrid(int columns, int rows, float overlap, std::vector<OutputMapping>& maps);

struct OutputBudget {
    size_t meshBytes = 0;
    size_t maskBytes = 0;
    size_t delayBytes = 0;
    size_t ndiBytes = 0;
    size_t total() const { return meshBytes + maskBytes + delayBytes + ndiBytes; }
};

size_t ndiReadbackBytes(int width, int height);
OutputBudget estimateOutputBudget(const std::array<OutputSpec, kMaxOutputs>& outputs, int pictureWidth, int pictureHeight, int windowWidth, int windowHeight);
std::string defaultNdiName(int index);

OutputSpec defaultOutputSpec(int index);
std::array<OutputSpec, kMaxOutputs> defaultOutputs();

}
