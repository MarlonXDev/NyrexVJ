#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace nvj {

enum class ValidationClass { Software, Virtual, Reported };

struct GraphicsInfo {
    std::string vendor;
    std::string renderer;
    std::string glVersion;
    std::string glslVersion;
};

struct RunContext {
    GraphicsInfo graphics;
    int width = 0;
    int height = 0;
    std::string quality;
    int targetFps = 0;
    double durationSeconds = 0.0;
    std::string scenario;
    std::vector<std::string> clips;
    std::vector<std::string> effects;
};

ValidationClass classifyRenderer(const std::string& vendor, const std::string& renderer);
const char* validationClassName(ValidationClass value);
std::string gpuVendorOf(const std::string& vendor, const std::string& renderer);
nlohmann::json runConditionsJson(const RunContext& context);

constexpr const char* kRunReportSchema = "nyrexvj-run-report";
constexpr int kRunReportSchemaVersion = 1;

}
