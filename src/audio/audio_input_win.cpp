#include "audio/audio_input.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace nvj {

namespace {

const PROPERTYKEY kFriendlyNameKey = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { reset(); }
    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }
    T** put() {
        reset();
        return &ptr_;
    }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

class ComScope {
public:
    ComScope() {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owned_ = result == S_OK || result == S_FALSE;
    }
    ~ComScope() {
        if (owned_) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool owned_ = false;
};

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string deviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.put()))) {
        return "Unknown device";
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::string name = "Unknown device";
    if (SUCCEEDED(store->GetValue(kFriendlyNameKey, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
        name = narrow(value.pwszVal);
    }
    PropVariantClear(&value);
    return name;
}

std::string deviceId(IMMDevice* device) {
    LPWSTR raw = nullptr;
    if (FAILED(device->GetId(&raw)) || !raw) {
        return std::string();
    }
    std::string id = narrow(raw);
    CoTaskMemFree(raw);
    return id;
}

void collect(IMMDeviceEnumerator* enumerator, EDataFlow flow, std::vector<AudioDevice>& out) {
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put()))) {
        return;
    }
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.put()))) {
            continue;
        }
        std::string id = deviceId(device.get());
        if (id.empty()) {
            continue;
        }
        AudioDevice entry;
        entry.loopback = flow == eRender;
        entry.id = std::string(entry.loopback ? "out:" : "in:") + id;
        entry.name = deviceName(device.get());
        out.push_back(entry);
    }
}

class WasapiInput final : public AudioInput {
public:
    ~WasapiInput() override { stop(); }

    std::vector<AudioDevice> listDevices() override {
        std::vector<AudioDevice> devices;
        ComScope com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put())))) {
            return devices;
        }
        collect(enumerator.get(), eRender, devices);
        collect(enumerator.get(), eCapture, devices);
        return devices;
    }

    bool start(const std::string& id, std::string& error) override {
        stop();
        bool loopback = false;
        std::string endpoint;
        if (id.rfind("out:", 0) == 0) {
            loopback = true;
            endpoint = id.substr(4);
        } else if (id.rfind("in:", 0) == 0) {
            endpoint = id.substr(3);
        } else {
            error = "Unknown audio device";
            return false;
        }
        auto ready = std::make_shared<std::promise<std::string>>();
        std::future<std::string> result = ready->get_future();
        stopRequested_ = false;
        failed_ = false;
        thread_ = std::thread([this, endpoint, loopback, ready]() { capture(widen(endpoint), loopback, ready); });
        std::string message = result.get();
        if (!message.empty()) {
            if (thread_.joinable()) {
                thread_.join();
            }
            error = message;
            return false;
        }
        running_ = true;
        return true;
    }

    void stop() override {
        stopRequested_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        running_ = false;
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

    bool running() const override { return running_ && !failed_; }
    int sampleRate() const override { return sampleRate_; }

    bool supportsChannel() const override { return true; }

    void setChannel(AudioChannel channel) override { channel_.store(channel); }

    void drain(std::vector<float>& mono) override {
        std::lock_guard<std::mutex> lock(mutex_);
        mono.swap(buffer_);
        buffer_.clear();
    }

private:
    void capture(const std::wstring& endpoint, bool loopback, std::shared_ptr<std::promise<std::string>> ready) {
        ComScope com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX* format = nullptr;

        auto fail = [&](const char* message) {
            if (format) {
                CoTaskMemFree(format);
            }
            ready->set_value(message);
        };

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put())))) {
            fail("Could not open the audio system");
            return;
        }
        if (FAILED(enumerator->GetDevice(endpoint.c_str(), device.put()))) {
            fail("Audio device not found");
            return;
        }
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put())))) {
            fail("Could not open the audio device");
            return;
        }
        if (FAILED(client->GetMixFormat(&format)) || !format) {
            fail("Could not read the audio format");
            return;
        }
        DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, format, nullptr))) {
            fail("Could not start capturing from this device");
            return;
        }
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.put())))) {
            fail("Could not start capturing from this device");
            return;
        }

        const int channels = std::max<int>(1, format->nChannels);
        const int bits = format->wBitsPerSample;
        bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
            const WAVEFORMATEXTENSIBLE* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            isFloat = extensible->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
        }
        sampleRate_ = static_cast<int>(format->nSamplesPerSec);
        const int bytesPerSample = bits / 8;
        const int frameBytes = bytesPerSample * channels;
        CoTaskMemFree(format);
        format = nullptr;

        if (FAILED(client->Start())) {
            ready->set_value("Could not start capturing from this device");
            return;
        }
        ready->set_value(std::string());

        std::vector<float> converted;
        while (!stopRequested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            UINT32 packet = 0;
            HRESULT status = capture->GetNextPacketSize(&packet);
            if (FAILED(status)) {
                failed_ = true;
                break;
            }
            while (packet > 0 && !stopRequested_) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr))) {
                    failed_ = true;
                    break;
                }
                converted.assign(frames, 0.0f);
                const AudioChannel mode = channel_.load();
                if (!(bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    for (UINT32 f = 0; f < frames; ++f) {
                        const BYTE* frame = data + static_cast<size_t>(f) * static_cast<size_t>(frameBytes);
                        float sum = 0.0f;
                        float first = 0.0f;
                        float second = 0.0f;
                        for (int c = 0; c < channels; ++c) {
                            const BYTE* sample = frame + c * bytesPerSample;
                            float value = 0.0f;
                            if (isFloat && bytesPerSample == 4) {
                                std::memcpy(&value, sample, 4);
                            } else if (bytesPerSample == 2) {
                                std::int16_t raw;
                                std::memcpy(&raw, sample, 2);
                                value = static_cast<float>(raw) / 32768.0f;
                            } else if (bytesPerSample == 3) {
                                std::int32_t raw = (static_cast<std::int32_t>(sample[2]) << 24) | (static_cast<std::int32_t>(sample[1]) << 16) | (static_cast<std::int32_t>(sample[0]) << 8);
                                value = static_cast<float>(raw) / 2147483648.0f;
                            } else if (bytesPerSample == 4) {
                                std::int32_t raw;
                                std::memcpy(&raw, sample, 4);
                                value = static_cast<float>(raw) / 2147483648.0f;
                            }
                            sum += value;
                            if (c == 0) {
                                first = value;
                            } else if (c == 1) {
                                second = value;
                            }
                        }
                        if (mode == AudioChannel::Left || (mode == AudioChannel::Right && channels < 2)) {
                            converted[f] = first;
                        } else if (mode == AudioChannel::Right) {
                            converted[f] = second;
                        } else {
                            converted[f] = sum / static_cast<float>(channels);
                        }
                    }
                }
                capture->ReleaseBuffer(frames);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    buffer_.insert(buffer_.end(), converted.begin(), converted.end());
                    const size_t limit = static_cast<size_t>(sampleRate_) * 2;
                    if (buffer_.size() > limit) {
                        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_.size() - limit));
                    }
                }
                if (FAILED(capture->GetNextPacketSize(&packet))) {
                    failed_ = true;
                    break;
                }
            }
            if (failed_) {
                break;
            }
        }
        client->Stop();
    }

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    std::atomic<int> sampleRate_{48000};
    std::atomic<AudioChannel> channel_{AudioChannel::Mix};
    std::mutex mutex_;
    std::vector<float> buffer_;
};

}

std::unique_ptr<AudioInput> createAudioInput() {
    return std::make_unique<WasapiInput>();
}

}
