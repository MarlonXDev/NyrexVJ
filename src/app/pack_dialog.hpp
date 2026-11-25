#pragma once

#include "app/show_dialog.hpp"
#include "core/pack_workflow.hpp"

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

enum class PackJobKind {
    None,
    Check,
    Install,
    PlanRemove,
    Remove,
    Build
};

struct PackJobResult {
    PackJobKind kind = PackJobKind::None;
    show::Inspection inspection;
    pack::InstallPlan plan;
    pack::InstallResult installed;
    pack::RemovePlan removal;
    pack::RemoveResult removed;
    pack::BuildResult built;
    std::string destination;
    std::vector<unsigned char> previewBytes;
    std::string licenseText;
    bool fresh = false;
};

struct PackDialog {
    bool open = false;
    bool focus = false;
    int requestedTab = -1;

    char packagePath[512] = {};
    bool checked = false;
    bool replan = false;
    show::Inspection inspection;
    pack::InstallPlan plan;
    int conflict = 0;
    bool acceptPlugin = false;
    bool allowDowngrade = false;
    bool hasResult = false;
    pack::InstallResult installed;
    std::string installMessage;
    std::string licenseText;
    unsigned previewTexture = 0;
    int previewWidth = 0;
    int previewHeight = 0;

    bool recordsDirty = true;
    std::vector<pack::Record> records;
    std::string selected;
    bool removalPlanned = false;
    pack::RemovePlan removal;
    std::string removeMessage;
    std::string installedLicense;
    std::string installedLicenseFor;

    char buildFolder[512] = {};
    char buildDestination[512] = {};
    bool buildOverwrite = false;
    std::string buildMessage;
    std::vector<std::string> buildProblems;
    bool buildOk = false;

    std::shared_ptr<ShowJobShared> shared;
    std::future<PackJobResult> job;
    PackJobKind running = PackJobKind::None;

    ~PackDialog() {
        if (shared) {
            shared->cancel = true;
        }
    }
};

}
