#include "control/midi_output.hpp"

#include <windows.h>
#include <mmsystem.h>

namespace nvj {

namespace {

std::string narrowName(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return std::string();
    }
    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

class WinMidiOutput final : public MidiOutput {
public:
    ~WinMidiOutput() override { close(); }

    std::vector<std::string> listDevices() override {
        std::vector<std::string> names;
        const UINT count = midiOutGetNumDevs();
        for (UINT i = 0; i < count; ++i) {
            MIDIOUTCAPSW caps{};
            if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                names.push_back(narrowName(caps.szPname));
            }
        }
        return names;
    }

    bool open(const std::string& name, std::string& error) override {
        close();
        const std::vector<std::string> names = listDevices();
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] != name) {
                continue;
            }
            HMIDIOUT opened = nullptr;
            if (midiOutOpen(&opened, static_cast<UINT>(i), 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
                error = "Could not open the MIDI output, another program may be using it";
                return false;
            }
            handle_ = opened;
            return true;
        }
        error = "The MIDI output device was not found";
        return false;
    }

    void close() override {
        if (handle_ != nullptr) {
            midiOutReset(handle_);
            midiOutClose(handle_);
            handle_ = nullptr;
        }
    }

    bool isOpen() const override { return handle_ != nullptr; }

    bool send(const MidiMessage& message) override {
        if (handle_ == nullptr) {
            return false;
        }
        const DWORD packed = static_cast<DWORD>(message.status & 0xFF) | (static_cast<DWORD>(message.data1 & 0x7F) << 8) | (static_cast<DWORD>(message.data2 & 0x7F) << 16);
        return midiOutShortMsg(handle_, packed) == MMSYSERR_NOERROR;
    }

private:
    HMIDIOUT handle_ = nullptr;
};

}

std::unique_ptr<MidiOutput> createMidiOutput() {
    return std::make_unique<WinMidiOutput>();
}

}
