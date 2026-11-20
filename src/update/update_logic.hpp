#pragma once

#include <string>
#include <vector>

namespace nvj {

constexpr const char* kRepoOwner = "MarlonXDev";
constexpr const char* kRepoName = "NyrexVJ";
constexpr const char* kWindowsAssetName = "NyrexVJ.exe";
constexpr const char* kLinuxAssetName = "NyrexVJ-linux-x64";
#ifdef _WIN32
constexpr const char* kExeAssetName = kWindowsAssetName;
#else
constexpr const char* kExeAssetName = kLinuxAssetName;
#endif
constexpr const char* kSumsAssetName = "SHA256SUMS.txt";

enum class UpdateChannel {
    Nightly = 0,
    Stable
};

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool dev = false;
    bool valid = false;
};

struct CurrentBuild {
    Version version;
    int buildNumber = 0;
};

struct ReleaseInfo {
    std::string tag;
    std::string name;
    std::string notes;
    std::string pageUrl;
    std::string exeUrl;
    std::string sumsUrl;
    Version version;
    int buildNumber = 0;
    bool prerelease = false;
    bool draft = false;
};

Version parseVersion(const std::string& text);
std::string versionToString(const Version& version);
int compareVersions(const Version& a, const Version& b);
int parseBuildNumber(const std::string& text);
bool parseRelease(const std::string& json, ReleaseInfo& release, std::string& error);
bool findHash(const std::string& sumsText, const std::string& fileName, std::string& hashHex);
bool looksLikeProgram(const std::vector<unsigned char>& data);
bool isUpdateAvailable(const ReleaseInfo& release, UpdateChannel channel, const CurrentBuild& current);
std::string releaseApiUrl(UpdateChannel channel);
const char* channelName(UpdateChannel channel);
UpdateChannel channelFromName(const std::string& name);
CurrentBuild currentBuild();

}
