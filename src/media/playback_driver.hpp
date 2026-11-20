#pragma once

#include "core/playhead.hpp"
#include "media/video_decoder.hpp"

namespace nvj {

class PlaybackDriver {
public:
    void setPlayback(const ClipPlayback& playback) { playback_ = clampedPlayback(playback); }
    const ClipPlayback& playback() const { return playback_; }
    void reset() { started_ = false; }
    void scrubTo(double position);
    void stepFrames(int frames);
    bool prepare(VideoDecoder& decoder, const VideoInfo& info, double& seconds);
    bool drive(VideoDecoder& decoder, const VideoInfo& info, double clock, double beat, double& seconds);
    double position() const { return started_ ? head_.position() : playback_.in; }
    const Playhead& head() const { return head_; }

    static double periodOf(const VideoInfo& info);

private:
    void apply(VideoDecoder& decoder) const;

    ClipPlayback playback_;
    Playhead head_;
    bool started_ = false;
    bool scrubPending_ = false;
    double scrubPosition_ = 0.0;
    int stepPending_ = 0;
    double clock_ = 0.0;
    double beat_ = 0.0;
};

}
