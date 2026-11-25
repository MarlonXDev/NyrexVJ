#pragma once

#include "core/show_workflow.hpp"

#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nvj {

struct ShowJobShared {
    std::atomic<unsigned long long> done{0};
    std::atomic<unsigned long long> total{0};
    std::atomic<bool> cancel{false};
    std::mutex mutex;
    std::string current;

    show::ProgressFn callback() {
        return [this](const show::Progress& progress) {
            done = progress.done;
            total = progress.total;
            {
                std::lock_guard<std::mutex> lock(mutex);
                current = progress.current;
            }
            return !cancel.load();
        };
    }

    std::string currentText() {
        std::lock_guard<std::mutex> lock(mutex);
        return current;
    }
};

enum class ShowJobKind {
    None,
    Write,
    Plan,
    Apply,
    Scan,
    Repair
};

struct ShowJobResult {
    ShowJobKind kind = ShowJobKind::None;
    show::WriteResult written;
    show::Inspection inspection;
    show::ImportPlan plan;
    show::ImportResult imported;
    show::RepairScan scan;
    std::vector<std::string> repaired;
    bool repairOk = false;
    std::string error;
    std::string destination;
};

struct ShowDialog {
    bool open = false;
    bool focus = false;
    int requestedTab = -1;

    char name[96] = {};
    char notes[512] = {};
    char destination[512] = {};
    bool overwrite = false;
    bool includeCues = false;
    bool includeTimeline = false;
    bool listsLoaded = false;
    std::vector<std::string> presets;
    std::vector<char> presetChosen;
    std::vector<std::string> venues;
    std::vector<char> venueChosen;
    bool planValid = false;
    show::ExportPlan exportPlan;
    std::vector<std::array<char, 200>> licenses;
    std::string exportMessage;

    char packagePath[512] = {};
    bool checked = false;
    show::Inspection inspection;
    show::ImportPlan importPlan;
    int policy = 0;
    bool applyCues = false;
    bool applyTimeline = false;
    bool replan = false;
    bool hasResult = false;
    show::ImportResult imported;
    std::string importMessage;
    char repairFolder[512] = {};
    show::RepairScan scan;
    std::vector<char> repairAccept;
    bool scanned = false;
    std::string repairMessage;

    std::shared_ptr<ShowJobShared> shared;
    std::future<ShowJobResult> job;
    ShowJobKind running = ShowJobKind::None;

    ~ShowDialog() {
        if (shared) {
            shared->cancel = true;
        }
    }
};

}
