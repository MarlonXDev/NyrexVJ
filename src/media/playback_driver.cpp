#include "media/playback_driver.hpp"

namespace nvj {

double PlaybackDriver::periodOf(const VideoInfo& info) {
    return info.fps > 1.0 ? 1.0 / info.fps : 1.0 / 30.0;
}

void PlaybackDriver::scrubTo(double position) {
    scrubPending_ = true;
    scrubPosition_ = position;
}

void PlaybackDriver::stepFrames(int frames) {
    stepPending_ += frames;
}

void PlaybackDriver::apply(VideoDecoder& decoder) const {
    decoder.setRange(head_.in(), head_.out());
    decoder.setDirection(head_.reverse());
}

bool PlaybackDriver::prepare(VideoDecoder& decoder, const VideoInfo& info, double& seconds) {
    if (!(info.duration > 0.0)) {
        seconds = 0.0;
        return false;
    }
    head_.configure(playback_, info.duration, periodOf(info));
    if (!started_) {
        head_.restart();
        started_ = true;
    }
    apply(decoder);
    seconds = head_.timeline();
    return true;
}

bool PlaybackDriver::drive(VideoDecoder& decoder, const VideoInfo& info, double clock, double beat, double& seconds) {
    const double previousClock = clock_;
    const double previousBeat = beat_;
    clock_ = clock;
    beat_ = beat;
    if (!(info.duration > 0.0)) {
        seconds = clock;
        return true;
    }
    head_.configure(playback_, info.duration, periodOf(info));
    if (!started_) {
        head_.restart();
        head_.advance(clock, 0.0);
        started_ = true;
    } else if (clock < previousClock - 1.0e-6) {
        head_.restart();
        head_.advance(clock, 0.0);
    } else {
        head_.advance(clock - previousClock, beat - previousBeat);
    }
    if (scrubPending_) {
        head_.seek(scrubPosition_);
        scrubPending_ = false;
    }
    if (stepPending_ != 0) {
        head_.step(stepPending_);
        stepPending_ = 0;
    }
    apply(decoder);
    seconds = head_.timeline();
    return head_.visible();
}

}
