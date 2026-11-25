#pragma once

#include "core/spatial_map.hpp"

#include <string>
#include <vector>

namespace nvj {

constexpr int kMinCalibrationPoints = 4;
constexpr int kMinCalibrationPointsWithFov = 6;
constexpr double kCalibrationPixelWidth = 1920.0;

enum class CalibrationStatus {
    Ok,
    TooFewPoints,
    Collinear,
    NotConverged,
};

struct CalibrationOptions {
    bool solveFov = false;
    int maxIterations = 200;
    double tolerance = 0.002;
    bool multiStart = true;
};

struct CalibrationResult {
    CalibrationStatus status = CalibrationStatus::TooFewPoints;
    SpatialDevice device;
    double rms = 0.0;
    double rmsPixels = 0.0;
    double maxError = 0.0;
    int iterations = 0;
    int starts = 0;
    int usedPoints = 0;
    std::vector<float> errors;
    std::string message;
};

const char* calibrationStatusName(CalibrationStatus status);
double calibrationError(const std::vector<CalibrationPoint>& points, const SpatialDevice& device, std::vector<float>* perPoint = nullptr);
CalibrationResult solveDevice(const std::vector<CalibrationPoint>& points, const SpatialDevice& initial, const CalibrationOptions& options = {});

}
