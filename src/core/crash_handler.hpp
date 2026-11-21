#pragma once

#include <string>
#include <vector>

namespace nvj {

constexpr int kCrashMarkerSlots = 32;
constexpr int kCrashMarkerIdLength = 48;

void installCrashHandler(const std::string& reportPath, const std::string& version);

int crashMarkerAcquire(const std::string& id);
void crashMarkerEnter(int slot);
void crashMarkerLeave(int slot);
void crashMarkerRelease(int slot);
std::vector<std::string> crashMarkerActive();
std::vector<std::string> pluginsInCrashReport(const std::string& report);

}
