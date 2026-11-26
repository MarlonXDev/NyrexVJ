#include "core/stoppable_thread.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace nvj {

struct StoppableThread::State {
    std::mutex mutex;
    std::condition_variable ended;
    bool finished = false;
    std::thread thread;
};

StoppableThread::StoppableThread() = default;

StoppableThread::~StoppableThread() {
    stop(3.0);
}

void StoppableThread::start(std::function<void()> body) {
    stop(3.0);
    const std::shared_ptr<State> state = std::make_shared<State>();
    state->thread = std::thread([state, body = std::move(body)]() {
        body();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->finished = true;
        }
        state->ended.notify_all();
    });
    state_ = state;
}

bool StoppableThread::stop(double waitSeconds) {
    const std::shared_ptr<State> state = std::move(state_);
    state_.reset();
    if (!state) {
        return true;
    }
    bool finished = false;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        finished = state->ended.wait_for(lock, std::chrono::duration<double>(waitSeconds), [&state]() { return state->finished; });
    }
    if (finished) {
        state->thread.join();
    } else {
        state->thread.detach();
    }
    return finished;
}

bool StoppableThread::running() const {
    return state_ != nullptr;
}

}
