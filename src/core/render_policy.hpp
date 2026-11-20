#pragma once

#include <string>
#include <vector>

namespace nvj {

constexpr float kHiddenOpacity = 0.001f;

enum QualityMode {
    QualityFull = 0,
    QualityBalanced = 1,
    QualityPerformance = 2,
    QualityAuto = 3,
    QualityModeCount
};

inline bool layerVisible(float opacity) {
    return opacity > kHiddenOpacity;
}

int clampQualityMode(int mode);
float fixedQualityScale(int mode);
int scaledDimension(int full, float scale);

class QualityController {
public:
    void reset();
    float update(double gpuMs, double budgetMs);
    float scale() const { return scale_; }
    int steps() const { return step_; }

private:
    static constexpr int kMaxStep = 4;
    int step_ = 0;
    float scale_ = 1.0f;
    double average_ = 0.0;
    bool primed_ = false;
    int over_ = 0;
    int under_ = 0;
    int cooldown_ = 0;
};

class IdleRelease {
public:
    explicit IdleRelease(int frames = 300) : limit_(frames) {}
    bool update(bool needed);
    void reset() { idle_ = 0; }
    int idleFrames() const { return idle_; }

private:
    int limit_ = 300;
    int idle_ = 0;
};

enum class PassKind {
    Housekeeping,
    Source,
    Effect,
    Composite,
    Preview,
    Final,
    Transition,
    Other
};

struct ProfileSample {
    PassKind kind = PassKind::Other;
    int deck = 0;
    int layer = 0;
    int slot = 0;
    std::string name;
    double ms = 0.0;
};

struct ProfileRow {
    PassKind kind = PassKind::Other;
    int deck = 0;
    int layer = 0;
    int slot = 0;
    std::string name;
    double averageMs = 0.0;
    double lastMs = 0.0;
    double share = 0.0;
    int missing = 0;
};

class ProfileTable {
public:
    void beginFrame();
    void add(const ProfileSample& sample);
    void endFrame();
    void clear();
    const std::vector<ProfileRow>& rows() const { return rows_; }
    std::vector<ProfileRow> sorted() const;
    double averageTotalMs() const { return averageTotal_; }
    double lastTotalMs() const { return lastTotal_; }
    int frames() const { return frames_; }

private:
    std::vector<ProfileRow> rows_;
    std::vector<bool> seen_;
    double averageTotal_ = 0.0;
    double lastTotal_ = 0.0;
    double frameTotal_ = 0.0;
    int frames_ = 0;
};

}
