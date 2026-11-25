#include "control/api_hub.hpp"

namespace nvj::api {

bool Ticket::begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Pending) {
        return false;
    }
    state_ = State::Running;
    return true;
}

void Ticket::complete(const Result& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = result;
    state_ = State::Done;
}

bool Ticket::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Pending) {
        return false;
    }
    state_ = State::Cancelled;
    return true;
}

bool Ticket::poll(Result& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Done) {
        return false;
    }
    result = result_;
    return true;
}

Ticket::State Ticket::state() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void Hub::publish(std::shared_ptr<const Snapshot> snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snapshot);
}

std::shared_ptr<const Snapshot> Hub::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::shared_ptr<Ticket> Hub::submit(const Command& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kMaxQueuedCommands) {
        return nullptr;
    }
    auto ticket = std::make_shared<Ticket>();
    queue_.emplace_back(command, ticket);
    return ticket;
}

size_t Hub::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void Hub::clear() {
    std::deque<std::pair<Command, std::shared_ptr<Ticket>>> dropped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dropped.swap(queue_);
    }
    for (auto& item : dropped) {
        item.second->cancel();
    }
}

void Hub::reset() {
    clear();
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.reset();
}

}
