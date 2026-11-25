#include "check.hpp"
#include "core/record_plan.hpp"
#include "core/settings.hpp"
#include "media/video_decoder.hpp"
#include "media/video_recorder.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG
#include "media/ffmpeg_api.hpp"
#include "media/frame_convert.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace nvj;

bool videoRequired() {
    const char* value = std::getenv("NYREXVJ_REQUIRE_VIDEO");
    return value != nullptr && *value != '\0';
}

struct Lcg {
    uint64_t state = 99;

    double next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
    }
};

void testProfiles() {
    std::vector<std::string> ids;
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        const RecordProfileInfo& info = recordProfileInfo(profile);
        CHECK(info.profile == profile);
        CHECK(std::find(ids.begin(), ids.end(), info.id) == ids.end());
        ids.push_back(info.id);
        const RecordProfileInfo* found = findRecordProfile(info.id);
        CHECK(found != nullptr && found->profile == profile);
        CHECK(info.sizeMultiple == 2 || info.sizeMultiple == 4);
        CHECK(info.audio != RecordAudioCodec::None);
        CHECK(std::string(info.extension) == "mp4" || std::string(info.extension) == "mov" || std::string(info.extension) == "mkv");
        CHECK(info.fragmentable == (std::string(info.container) != "matroska"));
    }
    CHECK(findRecordProfile("") == nullptr);
    CHECK(findRecordProfile("H264") == nullptr);
    CHECK(findRecordProfile("nonsense") == nullptr);
    CHECK(recordProfileInfo(static_cast<RecordProfile>(99)).profile == RecordProfile::H264Software);

    int alphaCount = 0;
    int hardwareCount = 0;
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfileInfo& info = recordProfileInfo(static_cast<RecordProfile>(i));
        alphaCount += info.alphaCapable ? 1 : 0;
        hardwareCount += info.hardware ? 1 : 0;
    }
    CHECK(alphaCount == 4);
    CHECK(hardwareCount == 2);
    CHECK(recordProfileInfo(RecordProfile::ProRes4444).alphaCapable);
    CHECK(recordProfileInfo(RecordProfile::HapAlpha).alphaCapable);
    CHECK(recordProfileInfo(RecordProfile::Ffv1).alphaCapable);
    CHECK(recordProfileInfo(RecordProfile::Vp9).alphaCapable);
    CHECK(!recordProfileInfo(RecordProfile::H264Software).alphaCapable);
    CHECK(!recordProfileInfo(RecordProfile::Hap).alphaCapable);
    CHECK(recordProfileInfo(RecordProfile::Hap).sizeMultiple == 4);
    CHECK(recordProfileInfo(RecordProfile::HapQ).sizeMultiple == 4);
    CHECK(recordProfileInfo(RecordProfile::H264Software).sizeMultiple == 2);
    CHECK(recordProfileInfo(RecordProfile::H264Software).audio == RecordAudioCodec::Aac);
    CHECK(recordProfileInfo(RecordProfile::ProResHq).audio == RecordAudioCodec::Pcm16);
    CHECK(recordProfileInfo(RecordProfile::H264Software).usesQuality);
    CHECK(!recordProfileInfo(RecordProfile::ProResHq).usesQuality);
}

void testSettingsCheck() {
    RecordSettings request;
    RecordCheck check = checkRecordSettings(request, 1920, 1080);
    CHECK(check.valid && check.settings.width == 1920 && check.settings.height == 1080);
    CHECK(check.settings.fps == 60 && check.settings.quality == 70 && check.settings.crashSafe && !check.settings.alpha);

    check = checkRecordSettings(request, 0, 0);
    CHECK(!check.valid && !check.problem.empty());
    check = checkRecordSettings(request, 1920, 0);
    CHECK(!check.valid);

    request.width = 1280;
    request.height = 0;
    check = checkRecordSettings(request, 1920, 1080);
    CHECK(check.valid && check.settings.width == 1920 && check.settings.height == 1080);

    request.width = 1281;
    request.height = 721;
    check = checkRecordSettings(request, 1920, 1080);
    CHECK(check.valid && check.settings.width == 1280 && check.settings.height == 720);

    request.profile = RecordProfile::Hap;
    request.width = 1918;
    request.height = 1078;
    check = checkRecordSettings(request, 1920, 1080);
    CHECK(check.valid && check.settings.width == 1916 && check.settings.height == 1076);

    request.profile = RecordProfile::H264Software;
    request.width = 3840;
    request.height = 2160;
    CHECK(checkRecordSettings(request, 1920, 1080).valid);
    request.width = 3841;
    request.height = 2161;
    check = checkRecordSettings(request, 1920, 1080);
    CHECK(check.valid && check.settings.width == 3840 && check.settings.height == 2160);
    request.width = 4096;
    request.height = 2304;
    check = checkRecordSettings(request, 1920, 1080);
    CHECK(!check.valid && !check.problem.empty());
    request.width = 7680;
    request.height = 1080;
    CHECK(checkRecordSettings(request, 1920, 1080).valid);
    request.width = 7682;
    request.height = 1080;
    CHECK(!checkRecordSettings(request, 1920, 1080).valid);
    request.width = 8;
    request.height = 8;
    CHECK(!checkRecordSettings(request, 1920, 1080).valid);
    request.width = 16;
    request.height = 16;
    CHECK(checkRecordSettings(request, 1920, 1080).valid);
    request.width = 15;
    request.height = 16;
    CHECK(!checkRecordSettings(request, 1920, 1080).valid);

    request = RecordSettings();
    request.fps = 59;
    CHECK(checkRecordSettings(request, 1280, 720).settings.fps == 60);
    request.fps = 1;
    CHECK(checkRecordSettings(request, 1280, 720).settings.fps == 24);
    request.fps = 1000;
    CHECK(checkRecordSettings(request, 1280, 720).settings.fps == 120);
    request.fps = 40;
    CHECK(checkRecordSettings(request, 1280, 720).settings.fps == 48);
    request.fps = -5;
    CHECK(checkRecordSettings(request, 1280, 720).settings.fps == 24);
    for (int rate : kRecordFrameRates) {
        request.fps = rate;
        CHECK(checkRecordSettings(request, 1280, 720).settings.fps == rate);
    }

    request = RecordSettings();
    request.quality = 500;
    CHECK(checkRecordSettings(request, 1280, 720).settings.quality == 100);
    request.quality = -3;
    CHECK(checkRecordSettings(request, 1280, 720).settings.quality == 0);

    request = RecordSettings();
    request.alpha = true;
    CHECK(!checkRecordSettings(request, 1280, 720).settings.alpha);
    request.profile = RecordProfile::ProRes4444;
    CHECK(checkRecordSettings(request, 1280, 720).settings.alpha);
    request.profile = RecordProfile::Vp9;
    CHECK(checkRecordSettings(request, 1280, 720).settings.alpha);
    request.profile = RecordProfile::Hap;
    CHECK(!checkRecordSettings(request, 1280, 720).settings.alpha);
}

void testPoolAndNames() {
    CHECK(recordPoolSize(0) == 5);
    CHECK(recordPoolSize(1280u * 720u * 4u) == 10);
    CHECK(recordPoolSize(1920u * 1080u * 4u) == 10);
    CHECK(recordPoolSize(3840u * 2160u * 4u) == 8);
    CHECK(recordPoolSize(400u * 1024u * 1024u) == 5);
    RecordSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    CHECK(recordFrameBytes(settings) == 1920u * 1080u * 4u);
    settings.width = -1;
    CHECK(recordFrameBytes(settings) == 0);

    CHECK(recordFileName(RecordProfile::H264Software, 2026, 11, 25, 9, 5, 3) == "NyrexVJ_2026-11-25_09-05-03.mp4");
    CHECK(recordFileName(RecordProfile::ProResHq, 2026, 1, 2, 23, 59, 59) == "NyrexVJ_2026-01-02_23-59-59.mov");
    CHECK(recordFileName(RecordProfile::Ffv1, 2026, 1, 2, 3, 4, 5) == "NyrexVJ_2026-01-02_03-04-05.mkv");

    std::vector<std::string> taken;
    auto exists = [&taken](const std::string& path) { return std::find(taken.begin(), taken.end(), path) != taken.end(); };
    CHECK(uniqueRecordPath("out", "a.mp4", exists) == "out/a.mp4");
    CHECK(uniqueRecordPath("out/", "a.mp4", exists) == "out/a.mp4");
    CHECK(uniqueRecordPath("out\\", "a.mp4", exists) == "out\\a.mp4");
    CHECK(uniqueRecordPath("", "a.mp4", exists) == "a.mp4");
    taken.push_back("out/a.mp4");
    CHECK(uniqueRecordPath("out", "a.mp4", exists) == "out/a_2.mp4");
    taken.push_back("out/a_2.mp4");
    CHECK(uniqueRecordPath("out", "a.mp4", exists) == "out/a_3.mp4");
    CHECK(uniqueRecordPath("out", "noext", exists) == "out/noext");
    for (int i = 2; i <= 999; ++i) {
        taken.push_back("out/a_" + std::to_string(i) + ".mp4");
    }
    CHECK(uniqueRecordPath("out", "a.mp4", exists).empty());
}

void testScheduleSteady() {
    FrameSchedule schedule(60);
    CHECK(!schedule.started());
    Lcg random;
    int64_t previous = -1;
    for (int k = 0; k < 1200; ++k) {
        const double jitter = (random.next() - 0.5) * 0.006;
        const double time = 1000.123 + static_cast<double>(k) / 60.0 + (k == 0 ? 0.0 : jitter);
        const FrameDecision decision = schedule.consider(time);
        CHECK(decision.accept);
        CHECK(decision.index == k);
        CHECK(decision.missed == 0);
        CHECK(decision.index > previous);
        previous = decision.index;
    }
    CHECK(schedule.missedTotal() == 0 && schedule.surplusTotal() == 0);
    CHECK(schedule.last() == 1199);
    CHECK(schedule.started());
}

void testScheduleRates() {
    FrameSchedule fast(60);
    int accepted = 0;
    int64_t previous = -1;
    for (int k = 0; k < 1440; ++k) {
        const FrameDecision decision = fast.consider(static_cast<double>(k) / 144.0);
        if (decision.accept) {
            ++accepted;
            CHECK(decision.index > previous);
            CHECK(decision.missed == 0);
            previous = decision.index;
        }
    }
    CHECK(accepted >= 598 && accepted <= 602);
    CHECK(fast.missedTotal() == 0);
    CHECK(fast.surplusTotal() == 1440 - accepted);

    FrameSchedule half(30);
    accepted = 0;
    for (int k = 0; k < 600; ++k) {
        const FrameDecision decision = half.consider(static_cast<double>(k) / 60.0);
        if (decision.accept) {
            CHECK(decision.index == accepted);
            CHECK(k % 2 == 0);
            ++accepted;
        }
    }
    CHECK(accepted == 300);
    CHECK(half.missedTotal() == 0);

    FrameSchedule slow(60);
    for (int k = 0; k < 600; ++k) {
        slow.consider(static_cast<double>(k) / 59.0);
    }
    CHECK(slow.missedTotal() >= 8 && slow.missedTotal() <= 12);
    CHECK(slow.surplusTotal() == 0);
}

void testScheduleStalls() {
    FrameSchedule schedule(60);
    CHECK(schedule.consider(10.0).accept);
    CHECK(schedule.consider(10.0 + 1.0 / 60.0).index == 1);
    const FrameDecision late = schedule.consider(10.5);
    CHECK(late.accept && late.index == 30 && late.missed == 28);
    CHECK(schedule.missedTotal() == 28);
    const FrameDecision same = schedule.consider(10.5);
    CHECK(!same.accept);
    CHECK(schedule.surplusTotal() == 1);
    const FrameDecision again = schedule.consider(10.5 + 1.0 / 60.0);
    CHECK(again.accept && again.index == 31 && again.missed == 0);

    CHECK(!schedule.consider(5.0).accept);
    CHECK(schedule.last() == 31);
    CHECK(!schedule.consider(std::nan("")).accept);
    CHECK(!schedule.consider(INFINITY).accept);
    CHECK(!schedule.wouldAccept(std::nan("")));
    CHECK(!schedule.wouldAccept(-INFINITY));
    CHECK(schedule.last() == 31);

    FrameSchedule fresh(30);
    CHECK(fresh.wouldAccept(0.0));
    CHECK(!fresh.wouldAccept(std::nan("")));
    const FrameDecision first = fresh.consider(123456.789);
    CHECK(first.accept && first.index == 0 && first.missed == 0);

    FrameSchedule clamped(0);
    CHECK(clamped.fps() == 1);
}

void testScheduleAgreement() {
    Lcg random;
    for (int run = 0; run < 50; ++run) {
        FrameSchedule left(30 + (run % 5) * 30);
        FrameSchedule right = left;
        double time = random.next() * 100.0;
        for (int step = 0; step < 400; ++step) {
            const bool backwards = random.next() < 0.03;
            time += backwards ? -0.01 : random.next() * 0.05;
            const bool predicted = left.wouldAccept(time);
            const FrameDecision decision = right.consider(time);
            CHECK(predicted == decision.accept);
            left.consider(time);
            CHECK(left.last() == right.last());
            CHECK(left.missedTotal() == right.missedTotal());
        }
    }
}

void testAudioSyncSteady() {
    AudioSync sync(48000);
    Lcg random;
    int64_t total = 0;
    for (int k = 0; k < 600; ++k) {
        const double wall = static_cast<double>(k + 1) * 800.0 / 48000.0 + (k == 0 ? 0.0 : (random.next() - 0.5) * 0.016);
        const int64_t delta = sync.adjust(wall, 800);
        CHECK(delta == 0);
        total += 800;
    }
    CHECK(sync.writtenSamples() == total);
    CHECK(sync.insertedSamples() == 0 && sync.droppedSamples() == 0);
    CHECK(sync.sampleRate() == 48000);
}

void testAudioSyncStart() {
    AudioSync late(48000);
    CHECK(late.adjust(0.1, 800) == 4000);
    CHECK(late.writtenSamples() == 4800);
    CHECK(late.insertedSamples() == 4000);

    AudioSync early(48000);
    CHECK(early.adjust(0.0, 4800) == -4800);
    CHECK(early.writtenSamples() == 0);
    CHECK(early.droppedSamples() == 4800);

    AudioSync partial(48000);
    CHECK(partial.adjust(0.05, 4800) == -2400);
    CHECK(partial.writtenSamples() == 2400);

    AudioSync exact(44100);
    CHECK(exact.adjust(800.0 / 44100.0, 800) == 0);
    CHECK(exact.writtenSamples() == 800);
}

void testAudioSyncDrift() {
    for (double speed : {0.995, 1.005}) {
        AudioSync sync(48000);
        double produced = 0.0;
        double wall = 0.0;
        for (int k = 0; k < 3600; ++k) {
            wall += 1.0 / 60.0;
            const double target = wall * 48000.0 * speed;
            const size_t count = static_cast<size_t>(target - produced);
            produced += static_cast<double>(count);
            sync.adjust(wall, count);
            const double drift = static_cast<double>(sync.writtenSamples()) / 48000.0 - wall;
            CHECK(std::fabs(drift) < 0.06);
        }
        const double expected = 60.0 * 0.005;
        if (speed < 1.0) {
            CHECK(std::fabs(static_cast<double>(sync.insertedSamples()) / 48000.0 - expected) < 0.06);
            CHECK(sync.droppedSamples() == 0);
        } else {
            CHECK(std::fabs(static_cast<double>(sync.droppedSamples()) / 48000.0 - expected) < 0.06);
            CHECK(sync.insertedSamples() == 0);
        }
    }

    AudioSync gap(48000);
    double wall = 0.0;
    for (int k = 0; k < 60; ++k) {
        wall += 800.0 / 48000.0;
        gap.adjust(wall, 800);
    }
    CHECK(gap.insertedSamples() == 0);
    wall += 2.0;
    const int64_t filled = gap.adjust(wall, 800);
    CHECK(filled > 94000 && filled < 98000);
    wall += 800.0 / 48000.0;
    CHECK(gap.adjust(wall, 800) == 0);

    AudioSync clamp(48000);
    clamp.adjust(0.0166, 800);
    const int64_t before = clamp.writtenSamples();
    const int64_t dropped = clamp.adjust(0.0166 + 0.001, 4800);
    CHECK(dropped < 0 && dropped >= -4800);
    CHECK(clamp.writtenSamples() == before + 4800 + dropped);
}

void testAudioSyncFinish() {
    AudioSync sync(48000);
    sync.adjust(4.9, static_cast<size_t>(4.9 * 48000));
    CHECK(sync.finish(5.0) == 4800);
    CHECK(sync.finish(4.9) == 0);
    CHECK(sync.finish(1.0) == 0);
    CHECK(sync.finish(-3.0) == 0);
    AudioSync none(48000);
    CHECK(none.finish(2.0) == 96000);

    CHECK(recordAudioRateSupported(RecordAudioCodec::Aac, 44100));
    CHECK(recordAudioRateSupported(RecordAudioCodec::Aac, 48000));
    CHECK(recordAudioRateSupported(RecordAudioCodec::Aac, 96000));
    CHECK(!recordAudioRateSupported(RecordAudioCodec::Aac, 192000));
    CHECK(!recordAudioRateSupported(RecordAudioCodec::Aac, 47999));
    CHECK(!recordAudioRateSupported(RecordAudioCodec::Aac, 0));
    CHECK(recordAudioRateSupported(RecordAudioCodec::Pcm16, 8000));
    CHECK(recordAudioRateSupported(RecordAudioCodec::Pcm16, 192000));
    CHECK(!recordAudioRateSupported(RecordAudioCodec::Pcm16, 7999));
    CHECK(!recordAudioRateSupported(RecordAudioCodec::Pcm16, 192001));
    CHECK(!recordAudioRateSupported(RecordAudioCodec::None, 48000));
}

#ifdef NYREXVJ_HAVE_FFMPEG

struct Sample {
    std::array<uint8_t, 16> rgba{};
};

struct Probe {
    bool ok = false;
    std::string error;
    std::string videoCodec;
    std::string audioCodec;
    int width = 0;
    int height = 0;
    int audioStreams = 0;
    int64_t audioSamples = 0;
    std::vector<int64_t> pts;
    std::vector<double> seconds;
    std::vector<Sample> samples;
};

Probe probeFile(const std::string& path, int fps, const char* preferredVideoDecoder = nullptr) {
    Probe probe;
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        probe.error = problem;
        return probe;
    }
    AVFormatContext* format = nullptr;
    if (api->avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        probe.error = "cannot open";
        return probe;
    }
    if (api->avformat_find_stream_info(format, nullptr) < 0) {
        api->avformat_close_input(&format);
        probe.error = "no stream info";
        return probe;
    }
    int videoIndex = -1;
    int audioIndex = -1;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVMediaType type = format->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && videoIndex < 0) {
            videoIndex = static_cast<int>(i);
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            ++probe.audioStreams;
            if (audioIndex < 0) {
                audioIndex = static_cast<int>(i);
            }
        }
    }
    if (videoIndex < 0) {
        api->avformat_close_input(&format);
        probe.error = "no video stream";
        return probe;
    }
    auto openDecoder = [&](int index, const char* preferred) -> AVCodecContext* {
        const AVCodec* codec = nullptr;
        if (preferred != nullptr) {
            codec = api->avcodec_find_decoder_by_name(preferred);
        }
        if (codec == nullptr) {
            codec = api->avcodec_find_decoder(format->streams[index]->codecpar->codec_id);
        }
        if (codec == nullptr) {
            return nullptr;
        }
        AVCodecContext* context = api->avcodec_alloc_context3(codec);
        api->avcodec_parameters_to_context(context, format->streams[index]->codecpar);
        if (api->avcodec_open2(context, codec, nullptr) < 0) {
            api->avcodec_free_context(&context);
            return nullptr;
        }
        return context;
    };
    AVCodecContext* video = openDecoder(videoIndex, preferredVideoDecoder);
    AVCodecContext* audio = audioIndex >= 0 ? openDecoder(audioIndex, nullptr) : nullptr;
    if (video == nullptr) {
        api->avformat_close_input(&format);
        probe.error = "no video decoder";
        return probe;
    }
    probe.videoCodec = api->avcodec_get_name(format->streams[videoIndex]->codecpar->codec_id);
    if (audioIndex >= 0) {
        probe.audioCodec = api->avcodec_get_name(format->streams[audioIndex]->codecpar->codec_id);
    }
    probe.width = video->width;
    probe.height = video->height;

    AVPacket* packet = api->av_packet_alloc();
    AVFrame* frame = api->av_frame_alloc();
    RgbaConverter converter;
    std::vector<uint8_t> rgba;

    auto takeVideo = [&]() {
        while (api->avcodec_receive_frame(video, frame) >= 0) {
            const int w = frame->width;
            const int h = frame->height;
            rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
            Sample sample;
            if (converter.convert(*api, *frame, rgba.data(), w * 4)) {
                const int xs[2] = {w / 4, 3 * w / 4};
                const int ys[2] = {h / 4, 3 * h / 4};
                for (int q = 0; q < 4; ++q) {
                    const size_t offset = (static_cast<size_t>(ys[q / 2]) * static_cast<size_t>(w) + static_cast<size_t>(xs[q % 2])) * 4u;
                    for (int c = 0; c < 4; ++c) {
                        sample.rgba[static_cast<size_t>(q * 4 + c)] = rgba[offset + static_cast<size_t>(c)];
                    }
                }
            }
            probe.samples.push_back(sample);
            const int64_t timestamp = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
            probe.pts.push_back(api->av_rescale_q(timestamp, format->streams[videoIndex]->time_base, AVRational{1, fps}));
            probe.seconds.push_back(static_cast<double>(api->av_rescale_q(timestamp, format->streams[videoIndex]->time_base, AVRational{1, 1000000})) / 1.0e6);
            api->av_frame_unref(frame);
        }
    };
    auto takeAudio = [&]() {
        while (audio != nullptr && api->avcodec_receive_frame(audio, frame) >= 0) {
            probe.audioSamples += frame->nb_samples;
            api->av_frame_unref(frame);
        }
    };

    while (api->av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == videoIndex) {
            if (api->avcodec_send_packet(video, packet) >= 0) {
                takeVideo();
            }
        } else if (packet->stream_index == audioIndex && audio != nullptr) {
            if (api->avcodec_send_packet(audio, packet) >= 0) {
                takeAudio();
            }
        }
        api->av_packet_unref(packet);
    }
    api->avcodec_send_packet(video, nullptr);
    takeVideo();
    if (audio != nullptr) {
        api->avcodec_send_packet(audio, nullptr);
        takeAudio();
    }

    api->av_packet_free(&packet);
    api->av_frame_free(&frame);
    api->avcodec_free_context(&video);
    if (audio != nullptr) {
        api->avcodec_free_context(&audio);
    }
    api->avformat_close_input(&format);
    probe.ok = true;
    return probe;
}

uint8_t grayFor(int64_t index) {
    return static_cast<uint8_t>(40 + (index * 5) % 180);
}

void fillFrame(uint8_t* data, int w, int h, int64_t index, bool alpha) {
    const uint8_t gray = grayFor(index);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            const bool right = x >= w / 2;
            const bool bottom = y >= h / 2;
            if (!right && !bottom) {
                pixel[0] = 220;
                pixel[1] = 40;
                pixel[2] = 40;
                pixel[3] = 255;
            } else if (right && !bottom) {
                pixel[0] = 40;
                pixel[1] = 200;
                pixel[2] = 40;
                pixel[3] = alpha ? 100 : 255;
            } else if (!right && bottom) {
                pixel[0] = 40;
                pixel[1] = 40;
                pixel[2] = 220;
                pixel[3] = alpha ? 30 : 255;
            } else {
                pixel[0] = gray;
                pixel[1] = gray;
                pixel[2] = gray;
                pixel[3] = 255;
            }
        }
    }
}

int tolerance(RecordProfile profile) {
    switch (profile) {
    case RecordProfile::Ffv1:
        return 3;
    case RecordProfile::ProResHq:
    case RecordProfile::ProRes4444:
        return 10;
    case RecordProfile::Hap:
    case RecordProfile::HapAlpha:
    case RecordProfile::HapQ:
        return 28;
    case RecordProfile::MotionJpeg:
        return 20;
    default:
        return 45;
    }
}

bool near(int actual, int expected, int allowed) {
    return std::abs(actual - expected) <= allowed;
}

void sleepMs(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void submitIndex(VideoRecorder& recorder, int w, int h, int64_t index, bool alpha) {
    uint8_t* frame = nullptr;
    for (int attempt = 0; attempt < 20000 && frame == nullptr; ++attempt) {
        frame = recorder.acquireFrame();
        if (frame == nullptr) {
            sleepMs(1);
        }
    }
    if (frame == nullptr) {
        CHECK(false);
        return;
    }
    fillFrame(frame, w, h, index, alpha);
    recorder.submitFrame(frame, index);
}

std::vector<float> sine(size_t count, size_t start, double rate) {
    std::vector<float> samples(count);
    for (size_t i = 0; i < count; ++i) {
        samples[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(start + i) / rate));
    }
    return samples;
}

RecordSettings testSettings(RecordProfile profile, bool alpha) {
    RecordSettings settings;
    settings.profile = profile;
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    settings.quality = 70;
    settings.alpha = alpha;
    settings.audio = true;
    return checkRecordSettings(settings, 640, 360).settings;
}

struct Recorded {
    RecordStats stats;
    std::vector<int64_t> indices;
};

Recorded recordClip(const RecordSettings& settings, const std::string& path, int audioRate, const std::vector<int64_t>& indices, double seconds, const RecorderTestHooks* hooks = nullptr) {
    Recorded result;
    VideoRecorder recorder;
    if (hooks != nullptr) {
        recorder.setTestHooks(*hooks);
    }
    std::string error;
    std::string detail;
    if (!recorder.open(settings, path, audioRate, error, detail)) {
        std::printf("open failed: %s (%s)\n", error.c_str(), detail.c_str());
        CHECK(false);
        return result;
    }
    size_t audioOffset = 0;
    const size_t audioTotal = audioRate > 0 ? static_cast<size_t>(seconds * audioRate) : 0;
    for (int64_t index : indices) {
        submitIndex(recorder, settings.width, settings.height, index, settings.alpha);
        result.indices.push_back(index);
        const double wallNow = static_cast<double>(index + 1) / static_cast<double>(settings.fps);
        while (audioOffset < audioTotal && static_cast<double>(audioOffset + 800) / audioRate <= wallNow) {
            const std::vector<float> chunk = sine(800, audioOffset, audioRate);
            audioOffset += 800;
            recorder.submitAudio(chunk.data(), chunk.size(), static_cast<double>(audioOffset) / audioRate);
        }
    }
    recorder.finish(seconds);
    CHECK(recorder.waitUntilDone(60.0));
    result.stats = recorder.stats();
    return result;
}

void verifyClip(RecordProfile profile, const std::string& path, const Recorded& recorded, bool alpha, bool expectAudio, const char* decoder = nullptr) {
    const RecordProfileInfo& info = recordProfileInfo(profile);
    CHECK(recorded.stats.state == RecordState::Finished);
    CHECK(recorded.stats.error.empty());
    CHECK(recorded.stats.stop == RecordStop::User);
    CHECK(recorded.stats.framesEncoded == recorded.indices.size());
    CHECK(recorded.stats.framesSubmitted == recorded.indices.size());
    CHECK(recorded.stats.bytes > 0);
    CHECK(fs::exists(fs::path(path)) && fs::file_size(fs::path(path)) > 1000);

    const Probe probe = probeFile(path, 30, decoder);
    if (!probe.ok) {
        std::printf("probe of %s failed: %s\n", info.id, probe.error.c_str());
    }
    CHECK(probe.ok);
    CHECK(probe.width == 640 && probe.height == 360);
    CHECK(probe.pts.size() == recorded.indices.size());
    const bool primed = expectAudio && info.audio == RecordAudioCodec::Aac;
    const double allowedShift = primed ? 0.025 : 0.002;
    if (probe.seconds.size() == recorded.indices.size()) {
        for (size_t i = 0; i < probe.seconds.size(); ++i) {
            const double expectedSeconds = static_cast<double>(recorded.indices[i]) / 30.0;
            const double off = std::fabs(probe.seconds[i] - expectedSeconds);
            if (off > allowedShift) {
                std::printf("profile %s: frame %zu is at %.4f s, expected %.4f s\n", info.id, i, probe.seconds[i], expectedSeconds);
            }
            CHECK(off <= allowedShift);
            if (i > 0) {
                CHECK(probe.seconds[i] > probe.seconds[i - 1]);
            }
        }
    }
    CHECK(probe.audioStreams == (expectAudio ? 1 : 0));
    if (expectAudio) {
        CHECK(std::llabs(probe.audioSamples - 96000) <= 2300);
    }
    const int allowed = tolerance(profile);
    int worst = 0;
    int checked = 0;
    int reported = 0;
    for (size_t i = 0; i < probe.samples.size() && i < recorded.indices.size(); ++i) {
        const auto& s = probe.samples[i].rgba;
        const int gray = grayFor(recorded.indices[i]);
        const int expected[16] = {220, 40, 40, 255, 40, 200, 40, alpha ? 100 : 255, 40, 40, 220, alpha ? 30 : 255, gray, gray, gray, 255};
        for (int c = 0; c < 16; ++c) {
            if (c % 4 == 3 && !alpha) {
                continue;
            }
            const int diff = std::abs(static_cast<int>(s[static_cast<size_t>(c)]) - expected[c]);
            worst = std::max(worst, diff);
            ++checked;
            if (diff > allowed && reported < 3) {
                ++reported;
                std::printf("profile %s: frame %lld channel %d is %d, expected %d\n", info.id, static_cast<long long>(recorded.indices[i]), c, static_cast<int>(s[static_cast<size_t>(c)]), expected[c]);
            }
            CHECK(diff <= allowed);
        }
    }
    CHECK(checked > 0);
    if (worst > allowed) {
        std::printf("profile %s: worst deviation %d, allowed %d\n", info.id, worst, allowed);
    }
}

std::vector<int64_t> gappedIndices() {
    std::vector<int64_t> indices;
    for (int64_t i = 0; i < 60; ++i) {
        if ((i >= 20 && i <= 22) || i == 45) {
            continue;
        }
        indices.push_back(i);
    }
    return indices;
}

void testCapabilities(const RecordCapabilities& caps) {
    VideoSupport support = videoSupport();
    CHECK(caps.ffmpegLoaded == (support.compiled && support.loaded));
    int working = 0;
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProbe& probe = caps.profiles[static_cast<size_t>(i)];
        if (probe.works) {
            ++working;
            CHECK(!probe.encoder.empty() && probe.detail.empty());
        } else {
            CHECK(probe.encoder.empty());
        }
        CHECK(caps.available(static_cast<RecordProfile>(i)) == probe.works);
        std::printf("  %-11s %s %s\n", recordProfileInfo(static_cast<RecordProfile>(i)).id, probe.works ? "works with" : "not available:", probe.works ? probe.encoder.c_str() : probe.detail.c_str());
    }
    if (videoRequired()) {
        CHECK(working >= 3);
    }
}

void testEveryProfile(const RecordCapabilities& caps, const fs::path& scratch) {
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        if (!caps.available(profile)) {
            continue;
        }
        const RecordProfileInfo& info = recordProfileInfo(profile);
        const std::string path = (scratch / (std::string("clip_") + info.id + "." + info.extension)).string();
        const RecordSettings settings = testSettings(profile, false);
        const Recorded recorded = recordClip(settings, path, 48000, gappedIndices(), 2.0);
        verifyClip(profile, path, recorded, false, true);
        CHECK(recorded.stats.audioActive);
        CHECK(recorded.stats.audioNote.empty());
        CHECK(std::fabs(recorded.stats.audioSeconds - 2.0) < 0.05);
        CHECK(recorded.stats.encoder == caps.profiles[static_cast<size_t>(i)].encoder);
        CHECK(recorded.stats.poolSize >= 5);
        std::printf("  %-11s ok: %llu frames, %llu bytes, encode %.1f ms avg\n", info.id, static_cast<unsigned long long>(recorded.stats.framesEncoded), static_cast<unsigned long long>(recorded.stats.bytes), recorded.stats.encodeMsAverage);
    }
}

void testAlpha(const RecordCapabilities& caps, const fs::path& scratch) {
    for (RecordProfile profile : {RecordProfile::ProRes4444, RecordProfile::HapAlpha, RecordProfile::Ffv1, RecordProfile::Vp9}) {
        if (!caps.available(profile)) {
            continue;
        }
        const RecordProfileInfo& info = recordProfileInfo(profile);
        const std::string path = (scratch / (std::string("alpha_") + info.id + "." + info.extension)).string();
        RecordSettings settings = testSettings(profile, true);
        CHECK(settings.alpha);
        std::vector<int64_t> indices;
        for (int64_t k = 0; k < 20; ++k) {
            indices.push_back(k);
        }
        if (profile == RecordProfile::Vp9) {
            const Recorded recorded = recordClip(settings, path, 0, indices, 0.7);
            CHECK(recorded.stats.state == RecordState::Finished);
            CHECK(recorded.stats.framesEncoded == 20);
            const Probe plain = probeFile(path, 30);
            CHECK(plain.ok && plain.pts.size() == 20);
            const Probe withAlpha = probeFile(path, 30, "libvpx-vp9");
            if (withAlpha.ok && withAlpha.pts.size() == 20) {
                int alphaChecks = 0;
                for (const Sample& sample : withAlpha.samples) {
                    CHECK(near(sample.rgba[3], 255, 6));
                    CHECK(near(sample.rgba[7], 100, 6));
                    CHECK(near(sample.rgba[11], 30, 6));
                    alphaChecks += 3;
                }
                CHECK(alphaChecks == 60);
            } else {
                std::printf("  vp9 alpha: the libvpx decoder is not part of this FFmpeg, the alpha plane was not read back\n");
            }
            continue;
        }
        const Recorded recorded = recordClip(settings, path, 0, indices, 0.7);
        CHECK(recorded.stats.state == RecordState::Finished);
        const Probe probe = probeFile(path, 30);
        CHECK(probe.ok && probe.pts.size() == 20);
        const int allowed = profile == RecordProfile::HapAlpha ? 28 : (profile == RecordProfile::Ffv1 ? 3 : 10);
        for (const Sample& sample : probe.samples) {
            CHECK(near(sample.rgba[3], 255, allowed));
            CHECK(near(sample.rgba[7], 100, allowed));
            CHECK(near(sample.rgba[11], 30, allowed));
        }
        std::printf("  %-11s alpha round trip ok\n", info.id);
    }
}

void testBackpressure(const RecordCapabilities& caps, const fs::path& scratch) {
    RecordProfile profile = RecordProfile::MotionJpeg;
    for (int i = 0; i < kRecordProfileCount; ++i) {
        if (caps.available(static_cast<RecordProfile>(i)) && !recordProfileInfo(static_cast<RecordProfile>(i)).hardware) {
            profile = static_cast<RecordProfile>(i);
            if (profile == RecordProfile::MotionJpeg) {
                break;
            }
        }
    }
    if (!caps.available(profile)) {
        return;
    }
    const RecordProfileInfo& info = recordProfileInfo(profile);
    const std::string path = (scratch / (std::string("pressure.") + info.extension)).string();
    RecordSettings settings = testSettings(profile, false);
    settings.audio = false;
    RecorderTestHooks hooks;
    hooks.encodeDelayMs = 40;
    VideoRecorder recorder;
    recorder.setTestHooks(hooks);
    std::string error;
    std::string detail;
    CHECK(recorder.open(settings, path, 0, error, detail));
    double worst = 0.0;
    uint64_t accepted = 0;
    uint64_t refused = 0;
    std::vector<int64_t> sent;
    for (int64_t index = 0; index < 150; ++index) {
        const auto began = std::chrono::steady_clock::now();
        uint8_t* frame = recorder.acquireFrame();
        double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        if (frame != nullptr) {
            fillFrame(frame, settings.width, settings.height, index, false);
            const auto submitBegan = std::chrono::steady_clock::now();
            recorder.submitFrame(frame, index);
            spent += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitBegan).count();
            ++accepted;
            sent.push_back(index);
        } else {
            ++refused;
        }
        worst = std::max(worst, spent);
        sleepMs(2);
    }
    const RecordStats during = recorder.stats();
    CHECK(during.queueDepthMax <= during.poolSize);
    CHECK(during.queueDepthMax >= 3);
    recorder.finish(5.0);
    CHECK(recorder.waitUntilDone(60.0));
    const RecordStats stats = recorder.stats();
    CHECK(stats.state == RecordState::Finished);
    CHECK(refused > 50);
    CHECK(stats.framesRejected == refused);
    CHECK(stats.framesSubmitted == accepted);
    CHECK(stats.framesEncoded == accepted);
    CHECK(worst < 15.0);
    const Probe probe = probeFile(path, 30);
    CHECK(probe.ok && probe.pts.size() == accepted);
    std::vector<int64_t> sorted = probe.pts;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted == sent);
    std::printf("  backpressure: %llu accepted, %llu refused, slowest call %.2f ms, deepest queue %d of %d\n", static_cast<unsigned long long>(accepted), static_cast<unsigned long long>(refused), worst, stats.queueDepthMax, stats.poolSize);
}

void testCancel(const RecordCapabilities& caps, const fs::path& scratch) {
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        if (!caps.available(profile) || recordProfileInfo(profile).hardware) {
            continue;
        }
        const std::string path = (scratch / (std::string("cancel.") + recordProfileInfo(profile).extension)).string();
        VideoRecorder recorder;
        std::string error;
        std::string detail;
        CHECK(recorder.open(testSettings(profile, false), path, 48000, error, detail));
        CHECK(fs::exists(fs::path(path)));
        for (int64_t k = 0; k < 8; ++k) {
            submitIndex(recorder, 640, 360, k, false);
        }
        recorder.cancel();
        recorder.cancel();
        CHECK(recorder.waitUntilDone(30.0));
        CHECK(recorder.stats().state == RecordState::Cancelled);
        CHECK(!fs::exists(fs::path(path)));
        CHECK(recorder.acquireFrame() == nullptr);
        recorder.finish(1.0);
        recorder.cancel();
        CHECK(recorder.stats().state == RecordState::Cancelled);
        break;
    }
}

void testEmptyAndEarlyStop(const RecordCapabilities& caps, const fs::path& scratch) {
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        if (!caps.available(profile) || recordProfileInfo(profile).hardware) {
            continue;
        }
        const std::string path = (scratch / (std::string("empty.") + recordProfileInfo(profile).extension)).string();
        {
            VideoRecorder recorder;
            std::string error;
            std::string detail;
            CHECK(recorder.open(testSettings(profile, false), path, 48000, error, detail));
            recorder.finish(0.0);
            CHECK(recorder.waitUntilDone(30.0));
            const RecordStats stats = recorder.stats();
            CHECK(stats.state == RecordState::Finished);
            CHECK(stats.framesEncoded == 0);
            CHECK(fs::exists(fs::path(path)));
        }
        {
            VideoRecorder recorder;
            std::string error;
            std::string detail;
            const std::string other = (scratch / (std::string("destructor.") + recordProfileInfo(profile).extension)).string();
            CHECK(recorder.open(testSettings(profile, false), other, 0, error, detail));
            for (int64_t k = 0; k < 6; ++k) {
                submitIndex(recorder, 640, 360, k, false);
            }
        }
        const Probe kept = probeFile((scratch / (std::string("destructor.") + recordProfileInfo(profile).extension)).string(), 30);
        CHECK(kept.ok && kept.pts.size() == 6);
        break;
    }
}

void testWriteFailure(const RecordCapabilities& caps, const fs::path& scratch) {
    for (RecordProfile profile : {RecordProfile::H264Software, RecordProfile::MotionJpeg, RecordProfile::ProResHq}) {
        if (!caps.available(profile)) {
            continue;
        }
        const std::string path = (scratch / (std::string("failing.") + recordProfileInfo(profile).extension)).string();
        RecorderTestHooks hooks;
        hooks.failWritesAfter = 5;
        VideoRecorder recorder;
        recorder.setTestHooks(hooks);
        std::string error;
        std::string detail;
        RecordSettings settings = testSettings(profile, false);
        settings.audio = false;
        CHECK(recorder.open(settings, path, 0, error, detail));
        for (int64_t k = 0; k < 60; ++k) {
            uint8_t* frame = recorder.acquireFrame();
            if (frame != nullptr) {
                fillFrame(frame, settings.width, settings.height, k, false);
                recorder.submitFrame(frame, k);
            }
            sleepMs(3);
        }
        CHECK(recorder.waitUntilDone(30.0));
        const RecordStats stats = recorder.stats();
        CHECK(stats.state == RecordState::Failed);
        CHECK(stats.stop == RecordStop::Error);
        CHECK(stats.error == "The file cannot be written");
        CHECK(!stats.detail.empty());
        CHECK(recorder.acquireFrame() == nullptr);
        CHECK(fs::exists(fs::path(path)));
        const Probe probe = probeFile(path, 30);
        CHECK(probe.ok);
        CHECK(probe.pts.size() >= 1 && probe.pts.size() <= 5);
        recorder.finish(1.0);
        CHECK(recorder.stats().state == RecordState::Failed);
        break;
    }
}

void testDiskLow(const RecordCapabilities& caps, const fs::path& scratch) {
    for (RecordProfile profile : {RecordProfile::MotionJpeg, RecordProfile::ProResHq, RecordProfile::H264Software}) {
        if (!caps.available(profile)) {
            continue;
        }
        const std::string path = (scratch / (std::string("disk.") + recordProfileInfo(profile).extension)).string();
        RecorderTestHooks hooks;
        hooks.minFreeBytes = UINT64_MAX / 2;
        VideoRecorder recorder;
        recorder.setTestHooks(hooks);
        std::string error;
        std::string detail;
        RecordSettings settings = testSettings(profile, false);
        settings.audio = false;
        CHECK(recorder.open(settings, path, 0, error, detail));
        int64_t k = 0;
        const auto began = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - began < std::chrono::seconds(4)) {
            const RecordState state = recorder.stats().state;
            if (state != RecordState::Recording) {
                break;
            }
            uint8_t* frame = recorder.acquireFrame();
            if (frame != nullptr) {
                fillFrame(frame, settings.width, settings.height, k, false);
                recorder.submitFrame(frame, k);
                ++k;
            }
            sleepMs(10);
        }
        CHECK(recorder.waitUntilDone(30.0));
        const RecordStats stats = recorder.stats();
        CHECK(stats.state == RecordState::Finished);
        CHECK(stats.stop == RecordStop::DiskFull);
        CHECK(stats.error == "The disk is almost full, the recording was stopped");
        CHECK(k > 0);
        const Probe probe = probeFile(path, 30);
        CHECK(probe.ok && probe.pts.size() >= 1);
        break;
    }
}

void testOpenFailures(const RecordCapabilities& caps, const fs::path& scratch) {
    const std::string blocker = (scratch / "blocker").string();
    {
        std::ofstream file(blocker);
        file << "not a folder";
    }
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        if (!caps.available(profile) || recordProfileInfo(profile).hardware) {
            continue;
        }
        VideoRecorder recorder;
        std::string error;
        std::string detail;
        const std::string path = blocker + "/inside.mp4";
        CHECK(!recorder.open(testSettings(profile, false), path, 0, error, detail));
        CHECK(error == "The file cannot be created");
        CHECK(!detail.empty());
        CHECK(recorder.stats().state == RecordState::Idle);
        CHECK(recorder.acquireFrame() == nullptr);
        CHECK(recorder.waitUntilDone(1.0));
        break;
    }
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        if (caps.available(profile)) {
            continue;
        }
        VideoRecorder recorder;
        std::string error;
        std::string detail;
        const std::string path = (scratch / "unavailable.mp4").string();
        CHECK(!recorder.open(testSettings(profile, false), path, 0, error, detail));
        CHECK(error == "No encoder for this format works on this computer");
        CHECK(!detail.empty());
        CHECK(!fs::exists(fs::path(path)));
        std::printf("  %s refused cleanly: %s\n", recordProfileInfo(profile).id, detail.c_str());
    }
}

void testAudioRate(const RecordCapabilities& caps, const fs::path& scratch) {
    for (RecordProfile profile : {RecordProfile::H264Software, RecordProfile::Vp9}) {
        if (!caps.available(profile)) {
            continue;
        }
        const std::string path = (scratch / (std::string("badrate.") + recordProfileInfo(profile).extension)).string();
        std::vector<int64_t> indices;
        for (int64_t k = 0; k < 10; ++k) {
            indices.push_back(k);
        }
        const Recorded recorded = recordClip(testSettings(profile, false), path, 192000, indices, 0.4);
        CHECK(recorded.stats.state == RecordState::Finished);
        CHECK(!recorded.stats.audioActive);
        CHECK(recorded.stats.audioNote == "The sample rate of the audio source cannot be recorded in this format");
        const Probe probe = probeFile(path, 30);
        CHECK(probe.ok && probe.audioStreams == 0 && probe.pts.size() == 10);
        break;
    }
}

void testAudioOverflow(const RecordCapabilities& caps, const fs::path& scratch) {
    for (RecordProfile profile : {RecordProfile::MotionJpeg, RecordProfile::ProResHq, RecordProfile::H264Software}) {
        if (!caps.available(profile)) {
            continue;
        }
        const std::string path = (scratch / (std::string("flood.") + recordProfileInfo(profile).extension)).string();
        VideoRecorder recorder;
        std::string error;
        std::string detail;
        CHECK(recorder.open(testSettings(profile, false), path, 48000, error, detail));
        const std::vector<float> flood(4096u * 512u, 0.25f);
        recorder.submitAudio(flood.data(), flood.size(), 0.0);
        submitIndex(recorder, 640, 360, 0, false);
        recorder.finish(1.0);
        CHECK(recorder.waitUntilDone(30.0));
        const RecordStats stats = recorder.stats();
        CHECK(stats.state == RecordState::Finished);
        CHECK(stats.audioOverruns > 0);
        const Probe probe = probeFile(path, 30);
        CHECK(probe.ok && probe.pts.size() == 1);
        break;
    }
}

bool copyWhileWriting(const std::string& from, const std::string& to) {
    std::error_code error;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
    return !error;
}

void testCrashSafety(const RecordCapabilities& caps, const fs::path& scratch) {
    struct Case {
        RecordProfile profile;
        bool crashSafe;
    };
    for (const Case& test : {Case{RecordProfile::H264Software, true}, Case{RecordProfile::ProResHq, true}, Case{RecordProfile::H264Software, false}}) {
        if (!caps.available(test.profile)) {
            continue;
        }
        const RecordProfileInfo& info = recordProfileInfo(test.profile);
        const std::string path = (scratch / (std::string("crash_") + (test.crashSafe ? "safe_" : "plain_") + info.id + "." + info.extension)).string();
        const std::string copy = path + ".copy." + info.extension;
        RecordSettings settings = testSettings(test.profile, false);
        settings.crashSafe = test.crashSafe;
        settings.audio = false;
        VideoRecorder recorder;
        std::string error;
        std::string detail;
        CHECK(recorder.open(settings, path, 0, error, detail));
        const int total = 240;
        for (int64_t k = 0; k < total; ++k) {
            submitIndex(recorder, settings.width, settings.height, k, false);
        }
        for (int attempt = 0; attempt < 3000 && recorder.stats().framesEncoded < static_cast<uint64_t>(total); ++attempt) {
            sleepMs(5);
        }
        CHECK(recorder.stats().framesEncoded == static_cast<uint64_t>(total));
        CHECK(copyWhileWriting(path, copy));
        const Probe interrupted = probeFile(copy, 30);
        if (test.crashSafe) {
            CHECK(interrupted.ok);
            CHECK(interrupted.pts.size() >= 100 && interrupted.pts.size() < static_cast<size_t>(total));
            std::printf("  %-11s crash safe: a copy taken while recording plays %zu of %d frames\n", info.id, interrupted.pts.size(), total);
        } else {
            CHECK(!interrupted.ok || interrupted.pts.empty());
            std::printf("  %-11s not crash safe: a copy taken while recording is %s\n", info.id, interrupted.ok ? "empty" : "unreadable");
        }
        recorder.finish(8.0);
        CHECK(recorder.waitUntilDone(60.0));
        const Probe finished = probeFile(path, 30);
        CHECK(finished.ok && finished.pts.size() == static_cast<size_t>(total));
    }
}

void testHardwareProfilesFailCleanly(const RecordCapabilities& caps) {
    for (RecordProfile profile : {RecordProfile::H264Hardware, RecordProfile::HevcHardware}) {
        const RecordProbe& probe = caps.profiles[static_cast<size_t>(profile)];
        if (!probe.works) {
            CHECK(!probe.detail.empty());
            CHECK(probe.encoder.empty());
        }
    }
}

void testRecorderSuite(const fs::path& scratch) {
    const RecordCapabilities caps = probeRecordCapabilities();
    std::printf("recording profiles on this computer:\n");
    testCapabilities(caps);
    if (!caps.ffmpegLoaded) {
        CHECK(!videoRequired());
        std::printf("recorder tests skipped: FFmpeg is not available\n");
        return;
    }
    std::printf("every working profile, with gaps in the frame numbers and audio:\n");
    testEveryProfile(caps, scratch);
    std::printf("alpha channel round trip:\n");
    testAlpha(caps, scratch);
    std::printf("slow encoder, cancel, failures, crash safety:\n");
    testBackpressure(caps, scratch);
    testCancel(caps, scratch);
    testEmptyAndEarlyStop(caps, scratch);
    testWriteFailure(caps, scratch);
    testDiskLow(caps, scratch);
    testOpenFailures(caps, scratch);
    testAudioRate(caps, scratch);
    testAudioOverflow(caps, scratch);
    testCrashSafety(caps, scratch);
    testHardwareProfilesFailCleanly(caps);
}

#else

void testRecorderSuite(const fs::path&) {
    CHECK(!videoRequired());
    std::printf("recorder tests skipped: built without the FFmpeg headers\n");
    VideoRecorder recorder;
    std::string error;
    std::string detail;
    CHECK(!recorder.open(RecordSettings(), "x.mp4", 0, error, detail));
    CHECK(!error.empty());
    CHECK(recorder.acquireFrame() == nullptr);
    CHECK(!probeRecordCapabilities().ffmpegLoaded);
}

#endif

}


void testRecordSettingsStorage(const fs::path& scratch) {
    const fs::path dir = scratch / "settings_data";
    std::error_code ec;
    fs::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    Settings fresh;
    CHECK(fresh.recordProfile == "h264" && fresh.recordWidth == 0 && fresh.recordHeight == 0);
    CHECK(fresh.recordFps == 60 && fresh.recordQuality == 70 && fresh.recordAudio && fresh.recordCrashSafe && fresh.recordFolder.empty());

    Settings settings;
    settings.recordProfile = "prores";
    settings.recordWidth = 1920;
    settings.recordHeight = 1080;
    settings.recordFps = 30;
    settings.recordQuality = 85;
    settings.recordAudio = false;
    settings.recordCrashSafe = false;
    settings.recordFolder = "D:/Aufnahmen";
    CHECK(saveSettings(settings));
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(loaded.recordProfile == "prores" && loaded.recordWidth == 1920 && loaded.recordHeight == 1080);
    CHECK(loaded.recordFps == 30 && loaded.recordQuality == 85 && !loaded.recordAudio && !loaded.recordCrashSafe);
    CHECK(loaded.recordFolder == "D:/Aufnahmen");

    settings.recordProfile = "gibt_es_nicht";
    settings.recordWidth = 99999;
    settings.recordHeight = 0;
    settings.recordFps = 5000;
    settings.recordQuality = 400;
    CHECK(saveSettings(settings));
    Settings clamped;
    CHECK(loadSettings(clamped));
    CHECK(clamped.recordProfile == "h264");
    CHECK(clamped.recordWidth == 0 && clamped.recordHeight == 0);
    CHECK(clamped.recordFps == 120 && clamped.recordQuality == 100);
}

int main() {
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_record_test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);

    testProfiles();
    testSettingsCheck();
    testPoolAndNames();
    testScheduleSteady();
    testScheduleRates();
    testScheduleStalls();
    testScheduleAgreement();
    testAudioSyncSteady();
    testAudioSyncStart();
    testAudioSyncDrift();
    testAudioSyncFinish();
    testRecordSettingsStorage(scratch);
    testRecorderSuite(scratch);

    if (std::getenv("NYREXVJ_KEEP_RECORD_TEST") == nullptr) {
        fs::remove_all(scratch, ec);
    }
    return testing::finish();
}
