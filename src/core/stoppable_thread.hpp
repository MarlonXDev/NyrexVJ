#pragma once

#include <functional>
#include <memory>

namespace nvj {

class StoppableThread {
public:
    StoppableThread();
    ~StoppableThread();
    StoppableThread(const StoppableThread&) = delete;
    StoppableThread& operator=(const StoppableThread&) = delete;

    void start(std::function<void()> body);
    bool stop(double waitSeconds);
    bool running() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}
