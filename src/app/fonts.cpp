#include "app/fonts.hpp"

#include <imgui.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace nvj {

namespace {

std::vector<std::string> candidatePaths() {
    std::vector<std::string> paths;
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::string root = std::string(windir && *windir ? windir : "C:\\Windows") + "\\Fonts\\";
    for (const char* name : {"YuGothR.ttc", "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc", "YuGothL.ttc", "YuGothB.ttc"}) {
        paths.push_back(root + name);
    }
#elif defined(__APPLE__)
    paths.push_back("/System/Library/Fonts/Hiragino Sans GB.ttc");
    paths.push_back("/System/Library/Fonts/\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W3.ttc");
    paths.push_back("/Library/Fonts/Arial Unicode.ttf");
#else
    paths.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    paths.push_back("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");
    paths.push_back("/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc");
    paths.push_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    paths.push_back("/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc");
    paths.push_back("/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf");
    paths.push_back("/usr/share/fonts/truetype/takao-gothic/TakaoGothic.ttf");
    paths.push_back("/usr/share/fonts/truetype/fonts-japanese-gothic.ttf");
#endif
    return paths;
}


std::vector<std::string> sansCandidates() {
    std::vector<std::string> paths;
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::string root = std::string(windir && *windir ? windir : "C:\\Windows") + "\\Fonts\\";
    for (const char* name : {"segoeui.ttf", "tahoma.ttf", "arial.ttf"}) {
        paths.push_back(root + name);
    }
#elif defined(__APPLE__)
    paths.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    paths.push_back("/Library/Fonts/Arial.ttf");
    paths.push_back("/System/Library/Fonts/Helvetica.ttc");
#else
    paths.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    paths.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    paths.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    paths.push_back("/usr/share/fonts/noto/NotoSans-Regular.ttf");
    paths.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    paths.push_back("/usr/share/fonts/dejavu/DejaVuSans.ttf");
#endif
    return paths;
}

ImFont* sansFont = nullptr;
std::string cjkMergedPath;

bool mergeInto(ImFont* font, const std::string& path) {
    ImFontConfig config;
    config.MergeMode = true;
    config.DstFont = font;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &config) != nullptr;
}

}

std::string findSansFontPath() {
    for (const std::string& path : sansCandidates()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(std::filesystem::path(path), error)) {
            return path;
        }
    }
    return std::string();
}

bool useSansFont(bool enable) {
    ImGuiIO& io = ImGui::GetIO();
    if (!enable) {
        io.FontDefault = nullptr;
        return true;
    }
    if (!sansFont) {
        const std::string path = findSansFontPath();
        if (path.empty()) {
            return false;
        }
        if (io.Fonts->Fonts.Size == 0) {
            io.Fonts->AddFontDefault();
        }
        ImFontConfig config;
        sansFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &config);
        if (!sansFont) {
            return false;
        }
        if (!cjkMergedPath.empty()) {
            mergeInto(sansFont, cjkMergedPath);
        }
    }
    io.FontDefault = sansFont;
    return true;
}

std::string findCjkFontPath() {
    for (const std::string& path : candidatePaths()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(std::filesystem::path(path), error)) {
            return path;
        }
    }
    return std::string();
}

bool mergeCjkFont(const std::string& path) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.Size == 0) {
        io.Fonts->AddFontDefault();
    }
    cjkMergedPath = path;
    const bool base = mergeInto(io.Fonts->Fonts[0], path);
    if (sansFont) {
        mergeInto(sansFont, path);
    }
    return base;
}

}
