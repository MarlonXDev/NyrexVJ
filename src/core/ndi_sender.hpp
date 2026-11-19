#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

class NdiSender {
public:
    NdiSender();
    ~NdiSender();
    NdiSender(const NdiSender&) = delete;
    NdiSender& operator=(const NdiSender&) = delete;

    static bool available();
    static void recheck();
    static std::string loadedLibrary();
    static std::string loadError();
    static void setLibraryOverride(const std::string& path);

    bool start(const std::string& name);
    void stop();
    bool active() const;
    int connections() const;
    bool sendRgbaBottomUp(const uint8_t* rgba, int width, int height);
    const std::string& name() const;
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
