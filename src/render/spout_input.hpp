#pragma once

#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct SpoutReceiveStatus {
    bool connected = false;
    bool hasFrame = false;
    std::string sender;
    int width = 0;
    int height = 0;
    double fps = 0.0;
};

class SpoutInput {
public:
    SpoutInput();
    ~SpoutInput();
    SpoutInput(const SpoutInput&) = delete;
    SpoutInput& operator=(const SpoutInput&) = delete;

    static bool supported();
    static void setTestPattern(bool enabled);
    static bool testPattern();
    static const char* testSenderName();

    std::vector<std::string> senders();
    bool receive(const std::string& preferred, const std::string& excluded);
    void release();
    bool active() const;
    unsigned int texture() const;
    int width() const;
    int height() const;
    const SpoutReceiveStatus& status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
