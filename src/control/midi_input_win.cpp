#include "control/midi_input.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <mutex>

namespace nvj {

namespace {

std::string narrow(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return std::string();
    }
    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

class WinMidiInput final : public MidiInput {
public:
    ~WinMidiInput() override { stop(); }

    std::vector<std::string> listDevices() override {
        std::vector<std::string> names;
        const UINT count = midiInGetNumDevs();
        for (UINT i = 0; i < count; ++i) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                names.push_back(narrow(caps.szPname));
            }
        }
        return names;
    }

    bool start(const std::string& name, std::string& error) override {
        stop();
        const std::vector<std::string> names = listDevices();
        UINT index = 0;
        bool found = false;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                index = static_cast<UINT>(i);
                found = true;
                break;
            }
        }
        if (!found) {
            error = "The MIDI device was not found";
            return false;
        }
        HMIDIIN opened = nullptr;
        if (midiInOpen(&opened, index, reinterpret_cast<DWORD_PTR>(&WinMidiInput::callback), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
            error = "Could not open the MIDI device, another program may be using it";
            return false;
        }
        handle_ = opened;
        midiInStart(handle_);
        return true;
    }

    void stop() override {
        if (handle_ != nullptr) {
            midiInStop(handle_);
            midiInReset(handle_);
            midiInClose(handle_);
            handle_ = nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    bool running() const override { return handle_ != nullptr; }

    void drain(std::vector<MidiMessage>& messages) override {
        std::lock_guard<std::mutex> lock(mutex_);
        messages.insert(messages.end(), queue_.begin(), queue_.end());
        queue_.clear();
    }

private:
    static void CALLBACK callback(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
        if (message != MIM_DATA || instance == 0) {
            return;
        }
        WinMidiInput* self = reinterpret_cast<WinMidiInput*>(instance);
        MidiMessage midi;
        midi.status = static_cast<int>(param1 & 0xFF);
        midi.data1 = static_cast<int>((param1 >> 8) & 0xFF);
        midi.data2 = static_cast<int>((param1 >> 16) & 0xFF);
        midi.time = midiNow();
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->queue_.size() < 4096) {
            self->queue_.push_back(midi);
        }
    }

    HMIDIIN handle_ = nullptr;
    std::mutex mutex_;
    std::vector<MidiMessage> queue_;
};

}

std::unique_ptr<MidiInput> createMidiInput() {
    return std::make_unique<WinMidiInput>();
}

}
