#pragma once

#include <memory>
#include <string>

namespace nvj {

class SpoutOutput {
public:
    SpoutOutput();
    ~SpoutOutput();
    SpoutOutput(const SpoutOutput&) = delete;
    SpoutOutput& operator=(const SpoutOutput&) = delete;

    static bool supported();

    bool start(const std::string& name);
    void stop();
    void send(unsigned int texture, int width, int height);
    bool active() const;
    bool connected() const;
    const std::string& name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
