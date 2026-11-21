#include "app/bench_convert.hpp"

#include "core/log.hpp"
#include "media/ffmpeg_setup.hpp"
#include "media/hap_convert.hpp"
#include "platform/platform.hpp"
#include "scene3d/scene_pack.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) * 0.5;
}

bool parseSpec(const std::string& spec, int& runs, std::vector<std::string>& paths) {
    runs = 3;
    size_t start = 0;
    size_t digits = 0;
    while (digits < spec.size() && std::isdigit(static_cast<unsigned char>(spec[digits])) != 0) {
        ++digits;
    }
    if (digits > 0 && digits < spec.size() && spec[digits] == ':') {
        runs = std::clamp(std::atoi(spec.substr(0, digits).c_str()), 1, 50);
        start = digits + 1;
    }
    std::stringstream list(spec.substr(start));
    std::string item;
    while (std::getline(list, item, ',')) {
        if (!item.empty()) {
            paths.push_back(item);
        }
    }
    return !paths.empty();
}

}

int runConvertBenchmark(const std::string& spec, const std::string& format, const std::string& keepFolder, const std::string& outputPath) {
    int runs = 3;
    std::vector<std::string> paths;
    if (!parseSpec(spec, runs, paths)) {
        logLine("Convert benchmark: no files given, use RUNS:file1,file2");
        return 1;
    }
    const ffmpeg_setup::Capabilities caps = ffmpeg_setup::checkCapabilities();
    if (!caps.ready()) {
        logLine("Convert benchmark: FFmpeg with the DXV decoder, the HAP encoder and the MOV muxer is not available" + (caps.problem.empty() ? std::string() : ": " + caps.problem));
        return 1;
    }

    hapconv::Options options;
    options.preference = hapconv::targetFromName(format);
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path(ec) / ("nyrexvj_bench_convert_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch, ec);
    if (ec) {
        logLine("Convert benchmark: the scratch folder cannot be created");
        return 1;
    }

    json report;
    report["ffmpegVersion"] = caps.version;
    report["ffmpegLicense"] = caps.license;
    report["ffmpegFolder"] = caps.loadedFrom;
    report["runs"] = runs;
    report["threads"] = options.threads;
    report["verified"] = options.verify;
    report["files"] = json::array();

    int exitCode = 0;
    for (const std::string& path : paths) {
        json entry;
        entry["path"] = path;
        const hapconv::SourceInfo info = hapconv::inspectSource(path);
        if (!info.ok || !info.isDxv) {
            entry["failed"] = info.ok ? std::string("The video is not DXV") : info.error;
            report["files"].push_back(entry);
            logLine("Convert benchmark: " + path + " cannot be used: " + entry["failed"].get<std::string>());
            exitCode = 1;
            continue;
        }
        std::vector<double> wall;
        std::vector<double> decode;
        std::vector<double> encode;
        std::vector<double> cpu;
        hapconv::Result last;
        bool failed = false;
        for (int run = 0; run < runs; ++run) {
            const fs::path output = scratch / ("run_" + std::to_string(run) + ".mov");
            const double cpuStart = platform::processCpuSeconds();
            const auto started = std::chrono::steady_clock::now();
            last = hapconv::convertFile(path, pathToUtf8(output), options, nullptr);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            const double cpuSeconds = platform::processCpuSeconds() - cpuStart;
            const bool keep = last.ok && run == runs - 1 && !keepFolder.empty();
            if (keep) {
                const fs::path target = pathFromUtf8(keepFolder) / pathFromUtf8(pathToUtf8(pathFromUtf8(path).stem()) + "_hap.mov");
                fs::create_directories(target.parent_path(), ec);
                fs::remove(target, ec);
                fs::copy_file(output, target, ec);
                if (ec) {
                    logLine("Convert benchmark: the converted copy cannot be kept");
                } else {
                    entry["keptAs"] = pathToUtf8(target);
                }
            }
            fs::remove(output, ec);
            if (!last.ok) {
                entry["failed"] = last.error;
                logLine("Convert benchmark: the conversion of " + path + " failed: " + last.error);
                failed = true;
                exitCode = 1;
                break;
            }
            wall.push_back(seconds);
            decode.push_back(last.decodeSeconds);
            encode.push_back(last.encodeSeconds);
            cpu.push_back(cpuSeconds);
        }
        if (!failed) {
            const double wallMedian = median(wall);
            const double cpuMedian = median(cpu);
            entry["variant"] = hapconv::variantName(last.variant);
            entry["target"] = hapconv::targetName(last.format);
            entry["alphaKept"] = last.alphaKept;
            entry["snappy"] = last.snappy;
            entry["width"] = last.width;
            entry["height"] = last.height;
            entry["fps"] = last.fps;
            entry["frames"] = last.frames;
            entry["sourceBytes"] = last.sourceBytes;
            entry["bytes"] = last.bytes;
            entry["sizeRatio"] = last.sourceBytes > 0 ? static_cast<double>(last.bytes) / static_cast<double>(last.sourceBytes) : 0.0;
            entry["wallSeconds"] = wall;
            entry["medianWallSeconds"] = wallMedian;
            entry["medianDecodeSeconds"] = median(decode);
            entry["medianEncodeSeconds"] = median(encode);
            entry["medianProcessCpuSeconds"] = cpuMedian;
            entry["framesPerSecond"] = wallMedian > 0.0 ? last.frames / wallMedian : 0.0;
            entry["speedVersusPlayback"] = wallMedian > 0.0 && info.duration > 0.0 ? info.duration / wallMedian : 0.0;
            entry["cpuPercentOfOneCore"] = wallMedian > 0.0 ? cpuMedian / wallMedian * 100.0 : 0.0;
        }
        report["files"].push_back(entry);
    }
    fs::remove_all(scratch, ec);

    const std::string text = report.dump(2);
    logLine("Convert benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return exitCode;
}

}
