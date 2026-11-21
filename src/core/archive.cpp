#include "core/archive.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace nvj::archive {

namespace {

namespace fs = std::filesystem;

constexpr size_t kHistory = 32768;
constexpr size_t kOutputCapacity = 256 * 1024 + kHistory;
constexpr unsigned long long kMaxDecodedBytes = 4ull * 1024ull * 1024ull * 1024ull;

std::array<unsigned int, 256> makeCrcTable() {
    std::array<unsigned int, 256> table{};
    for (unsigned int i = 0; i < 256; ++i) {
        unsigned int value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
        }
        table[i] = value;
    }
    return table;
}

const std::array<unsigned int, 256>& crcTable() {
    static const std::array<unsigned int, 256> table = makeCrcTable();
    return table;
}

struct Huffman {
    short count[16];
    short symbol[288];
};

int construct(Huffman& h, const short* length, int n) {
    for (int len = 0; len <= 15; ++len) {
        h.count[len] = 0;
    }
    for (int symbol = 0; symbol < n; ++symbol) {
        h.count[length[symbol]]++;
    }
    if (h.count[0] == n) {
        return 0;
    }
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) {
            return left;
        }
    }
    short offsets[16];
    offsets[1] = 0;
    for (int len = 1; len < 15; ++len) {
        offsets[len + 1] = static_cast<short>(offsets[len] + h.count[len]);
    }
    for (int symbol = 0; symbol < n; ++symbol) {
        if (length[symbol] != 0) {
            h.symbol[offsets[length[symbol]]++] = static_cast<short>(symbol);
        }
    }
    return left;
}

const short kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class Inflater {
public:
    using Source = std::function<size_t(unsigned char*, size_t)>;
    using Sink = std::function<bool(const unsigned char*, size_t)>;

    Inflater(Source source, Sink sink, unsigned long long maxOutput)
        : source_(std::move(source)), sink_(std::move(sink)), maxOutput_(maxOutput), input_(64 * 1024), output_(kOutputCapacity) {}

    bool run(std::string& error) {
        bool last = false;
        while (!last && !failed_) {
            last = bits(1) != 0;
            const unsigned type = bits(2);
            if (failed_) {
                break;
            }
            if (type == 0) {
                stored();
            } else if (type == 1) {
                fixed();
            } else if (type == 2) {
                dynamic();
            } else {
                fail("The compressed data is damaged");
            }
        }
        if (!failed_ && !flush(outLength_)) {
            fail("Writing the extracted data failed");
        }
        if (failed_) {
            error = error_;
            return false;
        }
        return true;
    }

    unsigned long long produced() const { return produced_; }

private:
    void fail(const std::string& message) {
        if (!failed_) {
            failed_ = true;
            error_ = message;
        }
    }

    unsigned bits(int need) {
        std::uint64_t value = bitBuffer_;
        while (bitCount_ < need) {
            if (inputPosition_ == inputLength_) {
                inputLength_ = source_(input_.data(), input_.size());
                inputPosition_ = 0;
                if (inputLength_ == 0) {
                    fail("The compressed data ends too early");
                    return 0;
                }
            }
            value |= static_cast<std::uint64_t>(input_[inputPosition_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuffer_ = value >> need;
        bitCount_ -= need;
        return static_cast<unsigned>(value & ((1ull << need) - 1));
    }

    int decode(const Huffman& h) {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= static_cast<int>(bits(1));
            if (failed_) {
                return -1;
            }
            const int count = h.count[len];
            if (code - count < first) {
                return h.symbol[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool flush(size_t bytes) {
        if (bytes == 0) {
            return true;
        }
        return sink_(output_.data(), bytes);
    }

    void put(unsigned char value) {
        if (produced_ >= maxOutput_) {
            fail("The unpacked data is larger than announced");
            return;
        }
        output_[outLength_++] = value;
        ++produced_;
        if (outLength_ == kOutputCapacity) {
            const size_t keep = kHistory;
            if (!flush(outLength_ - keep)) {
                fail("Writing the extracted data failed");
                return;
            }
            std::memmove(output_.data(), output_.data() + (outLength_ - keep), keep);
            outLength_ = keep;
        }
    }

    void stored() {
        bitBuffer_ = 0;
        bitCount_ = 0;
        const unsigned length = bits(16);
        const unsigned inverse = bits(16);
        if (failed_) {
            return;
        }
        if (length != (~inverse & 0xFFFFu)) {
            fail("The compressed data is damaged");
            return;
        }
        unsigned remaining = length;
        while (remaining > 0 && !failed_) {
            if (inputPosition_ == inputLength_) {
                inputLength_ = source_(input_.data(), input_.size());
                inputPosition_ = 0;
                if (inputLength_ == 0) {
                    fail("The compressed data ends too early");
                    return;
                }
            }
            const size_t take = std::min<size_t>(remaining, inputLength_ - inputPosition_);
            for (size_t i = 0; i < take && !failed_; ++i) {
                put(input_[inputPosition_ + i]);
            }
            inputPosition_ += take;
            remaining -= static_cast<unsigned>(take);
        }
    }

    void codes(const Huffman& lengthCode, const Huffman& distanceCode) {
        for (;;) {
            int symbol = decode(lengthCode);
            if (failed_) {
                return;
            }
            if (symbol < 0) {
                fail("The compressed data is damaged");
                return;
            }
            if (symbol < 256) {
                put(static_cast<unsigned char>(symbol));
                if (failed_) {
                    return;
                }
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) {
                    fail("The compressed data is damaged");
                    return;
                }
                const int length = kLengthBase[symbol] + static_cast<int>(bits(kLengthExtra[symbol]));
                const int distanceSymbol = decode(distanceCode);
                if (failed_) {
                    return;
                }
                if (distanceSymbol < 0 || distanceSymbol >= 30) {
                    fail("The compressed data is damaged");
                    return;
                }
                const size_t distance = static_cast<size_t>(kDistBase[distanceSymbol]) + bits(kDistExtra[distanceSymbol]);
                if (failed_) {
                    return;
                }
                if (distance > outLength_) {
                    fail("The compressed data is damaged");
                    return;
                }
                for (int i = 0; i < length && !failed_; ++i) {
                    put(output_[outLength_ - distance]);
                }
                if (failed_) {
                    return;
                }
            }
        }
    }

    void fixed() {
        static Huffman lengthCode;
        static Huffman distanceCode;
        static bool built = false;
        if (!built) {
            short lengths[288];
            int symbol = 0;
            for (; symbol < 144; ++symbol) {
                lengths[symbol] = 8;
            }
            for (; symbol < 256; ++symbol) {
                lengths[symbol] = 9;
            }
            for (; symbol < 280; ++symbol) {
                lengths[symbol] = 7;
            }
            for (; symbol < 288; ++symbol) {
                lengths[symbol] = 8;
            }
            construct(lengthCode, lengths, 288);
            for (symbol = 0; symbol < 30; ++symbol) {
                lengths[symbol] = 5;
            }
            construct(distanceCode, lengths, 30);
            built = true;
        }
        codes(lengthCode, distanceCode);
    }

    void dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = static_cast<int>(bits(5)) + 257;
        const int ndist = static_cast<int>(bits(5)) + 1;
        const int ncode = static_cast<int>(bits(4)) + 4;
        if (failed_) {
            return;
        }
        if (nlen > 286 || ndist > 30) {
            fail("The compressed data is damaged");
            return;
        }
        short lengths[320];
        int index = 0;
        for (; index < ncode; ++index) {
            lengths[order[index]] = static_cast<short>(bits(3));
        }
        for (; index < 19; ++index) {
            lengths[order[index]] = 0;
        }
        if (failed_) {
            return;
        }
        Huffman lengthCode;
        if (construct(lengthCode, lengths, 19) != 0) {
            fail("The compressed data is damaged");
            return;
        }
        index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lengthCode);
            if (failed_) {
                return;
            }
            if (symbol < 0) {
                fail("The compressed data is damaged");
                return;
            }
            if (symbol < 16) {
                lengths[index++] = static_cast<short>(symbol);
            } else {
                int repeatLength = 0;
                int repeat = 0;
                if (symbol == 16) {
                    if (index == 0) {
                        fail("The compressed data is damaged");
                        return;
                    }
                    repeatLength = lengths[index - 1];
                    repeat = 3 + static_cast<int>(bits(2));
                } else if (symbol == 17) {
                    repeat = 3 + static_cast<int>(bits(3));
                } else {
                    repeat = 11 + static_cast<int>(bits(7));
                }
                if (failed_) {
                    return;
                }
                if (index + repeat > nlen + ndist) {
                    fail("The compressed data is damaged");
                    return;
                }
                while (repeat-- > 0) {
                    lengths[index++] = static_cast<short>(repeatLength);
                }
            }
        }
        if (lengths[256] == 0) {
            fail("The compressed data is damaged");
            return;
        }
        Huffman literalCode;
        int error = construct(literalCode, lengths, nlen);
        if (error != 0 && (error < 0 || nlen != literalCode.count[0] + literalCode.count[1])) {
            fail("The compressed data is damaged");
            return;
        }
        Huffman distanceCode;
        error = construct(distanceCode, lengths + nlen, ndist);
        if (error != 0 && (error < 0 || ndist != distanceCode.count[0] + distanceCode.count[1])) {
            fail("The compressed data is damaged");
            return;
        }
        codes(literalCode, distanceCode);
    }

    Source source_;
    Sink sink_;
    unsigned long long maxOutput_;
    std::vector<unsigned char> input_;
    size_t inputPosition_ = 0;
    size_t inputLength_ = 0;
    std::uint64_t bitBuffer_ = 0;
    int bitCount_ = 0;
    std::vector<unsigned char> output_;
    size_t outLength_ = 0;
    unsigned long long produced_ = 0;
    bool failed_ = false;
    std::string error_;
};

unsigned int readLe16(const unsigned char* p) {
    return static_cast<unsigned int>(p[0]) | (static_cast<unsigned int>(p[1]) << 8);
}

unsigned long long readLe32(const unsigned char* p) {
    return static_cast<unsigned long long>(p[0]) | (static_cast<unsigned long long>(p[1]) << 8) | (static_cast<unsigned long long>(p[2]) << 16) | (static_cast<unsigned long long>(p[3]) << 24);
}

bool prepareTarget(const fs::path& destination, const std::string& relative, fs::path& target, std::string& error) {
    if (!safeRelativePath(relative)) {
        error = "The archive contains an unsafe path: " + relative;
        return false;
    }
    target = destination / fs::path(relative);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "The folder cannot be created: " + target.parent_path().string();
        return false;
    }
    return true;
}

class TarReader {
public:
    TarReader(const fs::path& destination, const Options& options, Result& result) : destination_(destination), options_(options), result_(result) {}

    bool feed(const unsigned char* data, size_t size) {
        while (size > 0) {
            if (state_ == State::End) {
                return true;
            }
            if (state_ == State::Header) {
                const size_t take = std::min(size, size_t(512) - headerFill_);
                std::memcpy(header_ + headerFill_, data, take);
                headerFill_ += take;
                data += take;
                size -= take;
                if (headerFill_ == 512) {
                    headerFill_ = 0;
                    if (!processHeader()) {
                        return false;
                    }
                }
            } else if (state_ == State::Data) {
                const size_t take = static_cast<size_t>(std::min<unsigned long long>(remaining_, size));
                if (!consume(data, take)) {
                    return false;
                }
                data += take;
                size -= take;
                remaining_ -= take;
                if (remaining_ == 0 && !finishData()) {
                    return false;
                }
            } else {
                const size_t take = static_cast<size_t>(std::min<unsigned long long>(padding_, size));
                data += take;
                size -= take;
                padding_ -= take;
                if (padding_ == 0) {
                    state_ = State::Header;
                }
            }
        }
        return true;
    }

    bool finish() {
        if (output_.is_open()) {
            output_.close();
        }
        if (state_ == State::Data || (state_ == State::Header && headerFill_ != 0)) {
            result_.error = "The archive ends too early";
            return false;
        }
        return true;
    }

private:
    enum class State { Header, Data, Padding, End };
    enum class Kind { Skip, File, Meta };

    static unsigned long long parseNumber(const unsigned char* p, size_t n) {
        if (p[0] & 0x80) {
            unsigned long long value = p[0] & 0x7F;
            for (size_t i = 1; i < n; ++i) {
                value = (value << 8) | p[i];
            }
            return value;
        }
        size_t i = 0;
        while (i < n && p[i] == ' ') {
            ++i;
        }
        unsigned long long value = 0;
        for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i) {
            value = value * 8 + (p[i] - '0');
        }
        return value;
    }

    static std::string field(const unsigned char* p, size_t n) {
        size_t length = 0;
        while (length < n && p[length] != 0) {
            ++length;
        }
        return std::string(reinterpret_cast<const char*>(p), length);
    }

    bool headerChecksumOk() const {
        unsigned long long unsignedSum = 0;
        long long signedSum = 0;
        for (size_t i = 0; i < 512; ++i) {
            const unsigned char c = (i >= 148 && i < 156) ? ' ' : header_[i];
            unsignedSum += c;
            signedSum += static_cast<signed char>(c);
        }
        const unsigned long long stored = parseNumber(header_ + 148, 8);
        return stored == unsignedSum || static_cast<long long>(stored) == signedSum;
    }

    bool processHeader() {
        bool zero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (header_[i] != 0) {
                zero = false;
                break;
            }
        }
        if (zero) {
            state_ = State::End;
            return true;
        }
        if (!headerChecksumOk()) {
            result_.error = "The archive header is damaged";
            return false;
        }
        if (++entryCount_ > options_.maxEntries) {
            result_.error = "The archive has too many entries";
            return false;
        }
        const char type = static_cast<char>(header_[156]);
        unsigned long long size = parseNumber(header_ + 124, 12);
        std::string name = field(header_, 100);
        const bool ustar = std::memcmp(header_ + 257, "ustar", 5) == 0;
        if (ustar) {
            const std::string prefix = field(header_ + 345, 155);
            if (!prefix.empty()) {
                name = prefix + "/" + name;
            }
        }
        std::string link = field(header_ + 157, 100);
        if (!longName_.empty()) {
            name = longName_;
            longName_.clear();
        }
        if (!longLink_.empty()) {
            link = longLink_;
            longLink_.clear();
        }
        if (paxHasPath_) {
            name = paxPath_;
            paxHasPath_ = false;
        }
        if (paxHasLink_) {
            link = paxLink_;
            paxHasLink_ = false;
        }
        if (paxHasSize_) {
            size = paxSize_;
            paxHasSize_ = false;
        }
        padding_ = (512 - (size % 512)) % 512;
        kind_ = Kind::Skip;
        metaType_ = 0;
        remaining_ = size;
        if (type == 'L' || type == 'K' || type == 'x') {
            if (size > 1024 * 1024) {
                result_.error = "The archive header is too large";
                return false;
            }
            kind_ = Kind::Meta;
            metaType_ = type;
            meta_.clear();
        } else if (type == 'g' || type == '5') {
            kind_ = Kind::Skip;
        } else if (type == '0' || type == 0 || type == '7') {
            const std::string relative = options_.map ? options_.map(name, false) : name;
            if (!relative.empty()) {
                if (size > options_.maxEntryBytes || selectedBytes_ + size > options_.maxTotalBytes) {
                    result_.error = "The archive is larger than allowed";
                    return false;
                }
                fs::path target;
                if (!prepareTarget(destination_, relative, target, result_.error)) {
                    return false;
                }
                std::error_code ec;
                fs::remove(target, ec);
                output_.open(target, std::ios::binary | std::ios::trunc);
                if (!output_) {
                    result_.error = "The file cannot be created: " + target.string();
                    return false;
                }
                kind_ = Kind::File;
                currentRelative_ = relative;
                currentTarget_ = target;
                currentMode_ = static_cast<unsigned int>(parseNumber(header_ + 100, 8));
                selectedBytes_ += size;
            }
        } else if (type == '2') {
            const std::string relative = options_.map ? options_.map(name, false) : name;
            if (!relative.empty()) {
                if (!safeLinkTarget(link)) {
                    result_.error = "The archive contains an unsafe link: " + link;
                    return false;
                }
                fs::path target;
                if (!prepareTarget(destination_, relative, target, result_.error)) {
                    return false;
                }
                std::error_code ec;
                fs::remove(target, ec);
                fs::create_symlink(fs::path(link), target, ec);
                if (ec) {
                    result_.error = "The link cannot be created: " + relative;
                    return false;
                }
                result_.files.push_back(relative);
            }
        } else if (type == '1') {
            const std::string relative = options_.map ? options_.map(name, false) : name;
            if (!relative.empty()) {
                result_.error = "The archive contains a hard link, which is not supported";
                return false;
            }
        }
        if (remaining_ == 0) {
            return finishData();
        }
        state_ = State::Data;
        return true;
    }

    bool consume(const unsigned char* data, size_t size) {
        if (kind_ == Kind::File) {
            output_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!output_) {
                result_.error = "The file cannot be written (disk full?): " + currentTarget_.string();
                return false;
            }
            result_.bytes += size;
        } else if (kind_ == Kind::Meta) {
            meta_.append(reinterpret_cast<const char*>(data), size);
        }
        return true;
    }

    bool finishData() {
        if (kind_ == Kind::File) {
            output_.close();
            if (!output_) {
                result_.error = "The file cannot be written (disk full?): " + currentTarget_.string();
                return false;
            }
            std::error_code ec;
            fs::permissions(currentTarget_, static_cast<fs::perms>(currentMode_ & 0755), fs::perm_options::replace, ec);
            result_.files.push_back(currentRelative_);
        } else if (kind_ == Kind::Meta) {
            parseMeta();
        }
        kind_ = Kind::Skip;
        state_ = padding_ > 0 ? State::Padding : State::Header;
        return true;
    }

    void parseMeta() {
        std::string value = meta_;
        if (metaType_ == 'L' || metaType_ == 'K') {
            const size_t end = value.find('\0');
            if (end != std::string::npos) {
                value.resize(end);
            }
            if (metaType_ == 'L') {
                longName_ = value;
            } else {
                longLink_ = value;
            }
            return;
        }
        size_t position = 0;
        while (position < value.size()) {
            const size_t space = value.find(' ', position);
            if (space == std::string::npos) {
                break;
            }
            const unsigned long long length = std::strtoull(value.c_str() + position, nullptr, 10);
            if (length == 0 || position + length > value.size() || space >= position + length) {
                break;
            }
            std::string record = value.substr(space + 1, position + length - space - 1);
            if (!record.empty() && record.back() == '\n') {
                record.pop_back();
            }
            const size_t equals = record.find('=');
            if (equals != std::string::npos) {
                const std::string key = record.substr(0, equals);
                const std::string data = record.substr(equals + 1);
                if (key == "path") {
                    paxPath_ = data;
                    paxHasPath_ = true;
                } else if (key == "linkpath") {
                    paxLink_ = data;
                    paxHasLink_ = true;
                } else if (key == "size") {
                    paxSize_ = std::strtoull(data.c_str(), nullptr, 10);
                    paxHasSize_ = true;
                }
            }
            position += length;
        }
    }

    fs::path destination_;
    const Options& options_;
    Result& result_;
    State state_ = State::Header;
    Kind kind_ = Kind::Skip;
    unsigned char header_[512] = {0};
    size_t headerFill_ = 0;
    unsigned long long remaining_ = 0;
    unsigned long long padding_ = 0;
    size_t entryCount_ = 0;
    unsigned long long selectedBytes_ = 0;
    std::ofstream output_;
    std::string currentRelative_;
    fs::path currentTarget_;
    unsigned int currentMode_ = 0;
    char metaType_ = 0;
    std::string meta_;
    std::string longName_;
    std::string longLink_;
    std::string paxPath_;
    std::string paxLink_;
    unsigned long long paxSize_ = 0;
    bool paxHasPath_ = false;
    bool paxHasLink_ = false;
    bool paxHasSize_ = false;
};

#ifndef _WIN32

struct LzmaStream {
    const unsigned char* nextIn = nullptr;
    size_t availIn = 0;
    unsigned long long totalIn = 0;
    unsigned char* nextOut = nullptr;
    size_t availOut = 0;
    unsigned long long totalOut = 0;
    const void* allocator = nullptr;
    void* internal = nullptr;
    void* reservedPtr1 = nullptr;
    void* reservedPtr2 = nullptr;
    void* reservedPtr3 = nullptr;
    void* reservedPtr4 = nullptr;
    unsigned long long reservedInt1 = 0;
    unsigned long long reservedInt2 = 0;
    size_t reservedInt3 = 0;
    size_t reservedInt4 = 0;
    int reservedEnum1 = 0;
    int reservedEnum2 = 0;
};

using LzmaDecoderFn = int (*)(LzmaStream*, unsigned long long, unsigned int);
using LzmaCodeFn = int (*)(LzmaStream*, int);
using LzmaEndFn = void (*)(LzmaStream*);

struct LzmaApi {
    void* handle = nullptr;
    LzmaDecoderFn decoder = nullptr;
    LzmaCodeFn code = nullptr;
    LzmaEndFn end = nullptr;
};

bool loadLzma(LzmaApi& api) {
    static const char* const names[] = {"liblzma.so.5", "liblzma.so", "liblzma.5.dylib", "liblzma.dylib"};
    for (const char* name : names) {
        void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        api.handle = handle;
        api.decoder = reinterpret_cast<LzmaDecoderFn>(dlsym(handle, "lzma_stream_decoder"));
        api.code = reinterpret_cast<LzmaCodeFn>(dlsym(handle, "lzma_code"));
        api.end = reinterpret_cast<LzmaEndFn>(dlsym(handle, "lzma_end"));
        if (api.decoder && api.code && api.end) {
            return true;
        }
        dlclose(handle);
        api = LzmaApi{};
    }
    return false;
}

#endif

}

bool safeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 512) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return false;
    }
    if (path.find('\\') != std::string::npos || path.find(':') != std::string::npos || path.find('\0') != std::string::npos) {
        return false;
    }
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (char c : part) {
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

bool safeLinkTarget(const std::string& target) {
    if (target.empty() || target.size() > 255) {
        return false;
    }
    if (target == "." || target == "..") {
        return false;
    }
    return target.find('/') == std::string::npos && target.find('\\') == std::string::npos && target.find(':') == std::string::npos && target.find('\0') == std::string::npos;
}

bool xzAvailable() {
#ifndef _WIN32
    LzmaApi api;
    if (!loadLzma(api)) {
        return false;
    }
    dlclose(api.handle);
    return true;
#else
    return false;
#endif
}

unsigned int crc32Update(unsigned int crc, const unsigned char* data, size_t size) {
    const std::array<unsigned int, 256>& table = crcTable();
    unsigned int value = ~crc;
    for (size_t i = 0; i < size; ++i) {
        value = table[(value ^ data[i]) & 0xFF] ^ (value >> 8);
    }
    return ~value;
}

bool inflateRaw(const unsigned char* input, size_t inputSize, std::vector<unsigned char>& output, unsigned long long maxOutput, std::string& error) {
    output.clear();
    size_t position = 0;
    Inflater inflater(
        [&](unsigned char* buffer, size_t capacity) {
            const size_t take = std::min(capacity, inputSize - position);
            if (take > 0) {
                std::memcpy(buffer, input + position, take);
            }
            position += take;
            return take;
        },
        [&](const unsigned char* data, size_t size) {
            output.insert(output.end(), data, data + size);
            return true;
        },
        maxOutput);
    return inflater.run(error);
}

Result extractZip(const fs::path& archive, const fs::path& destination, const Options& options) {
    Result result;
    std::ifstream in(archive, std::ios::binary);
    if (!in) {
        result.error = "The archive cannot be opened";
        return result;
    }
    in.seekg(0, std::ios::end);
    const unsigned long long fileSize = static_cast<unsigned long long>(in.tellg());
    if (fileSize < 22) {
        result.error = "The archive is not a zip file";
        return result;
    }
    const size_t tailSize = static_cast<size_t>(std::min<unsigned long long>(fileSize, 22 + 65535));
    std::vector<unsigned char> tail(tailSize);
    in.seekg(static_cast<std::streamoff>(fileSize - tailSize));
    in.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tailSize));
    if (!in) {
        result.error = "The archive cannot be read";
        return result;
    }
    size_t eocd = std::string::npos;
    for (size_t i = tailSize - 22 + 1; i-- > 0;) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        result.error = "The archive is not a zip file";
        return result;
    }
    const unsigned int entryTotal = readLe16(&tail[eocd + 10]);
    const unsigned long long directorySize = readLe32(&tail[eocd + 12]);
    const unsigned long long directoryOffset = readLe32(&tail[eocd + 16]);
    if (entryTotal == 0xFFFF || directorySize == 0xFFFFFFFFull || directoryOffset == 0xFFFFFFFFull) {
        result.error = "Zip64 archives are not supported";
        return result;
    }
    if (directoryOffset + directorySize > fileSize || entryTotal > options.maxEntries) {
        result.error = "The archive is damaged";
        return result;
    }
    std::vector<unsigned char> directory(static_cast<size_t>(directorySize));
    in.seekg(static_cast<std::streamoff>(directoryOffset));
    in.read(reinterpret_cast<char*>(directory.data()), static_cast<std::streamsize>(directory.size()));
    if (!in) {
        result.error = "The archive cannot be read";
        return result;
    }

    struct Entry {
        std::string name;
        std::string relative;
        unsigned int method = 0;
        unsigned int flags = 0;
        unsigned int crc = 0;
        unsigned long long compressed = 0;
        unsigned long long size = 0;
        unsigned long long offset = 0;
        unsigned int attributes = 0;
    };
    std::vector<Entry> entries;
    unsigned long long totalSize = 0;
    size_t position = 0;
    for (unsigned int i = 0; i < entryTotal; ++i) {
        if (position + 46 > directory.size() || readLe32(&directory[position]) != 0x02014B50ull) {
            result.error = "The archive is damaged";
            return result;
        }
        const unsigned char* p = &directory[position];
        Entry entry;
        entry.flags = readLe16(p + 8);
        entry.method = readLe16(p + 10);
        entry.crc = static_cast<unsigned int>(readLe32(p + 16));
        entry.compressed = readLe32(p + 20);
        entry.size = readLe32(p + 24);
        const size_t nameLength = readLe16(p + 28);
        const size_t extraLength = readLe16(p + 30);
        const size_t commentLength = readLe16(p + 32);
        entry.attributes = static_cast<unsigned int>(readLe32(p + 38));
        entry.offset = readLe32(p + 42);
        if (position + 46 + nameLength + extraLength + commentLength > directory.size()) {
            result.error = "The archive is damaged";
            return result;
        }
        entry.name.assign(reinterpret_cast<const char*>(p + 46), nameLength);
        position += 46 + nameLength + extraLength + commentLength;
        const bool isDirectory = !entry.name.empty() && entry.name.back() == '/';
        if (isDirectory) {
            continue;
        }
        entry.relative = options.map ? options.map(entry.name, false) : entry.name;
        if (entry.relative.empty()) {
            continue;
        }
        if (!safeRelativePath(entry.relative)) {
            result.error = "The archive contains an unsafe path: " + entry.relative;
            return result;
        }
        if ((entry.flags & 1u) != 0) {
            result.error = "The archive is encrypted";
            return result;
        }
        if (entry.method != 0 && entry.method != 8) {
            result.error = "The archive uses an unsupported compression method";
            return result;
        }
        if (entry.size == 0xFFFFFFFFull || entry.compressed == 0xFFFFFFFFull || entry.offset == 0xFFFFFFFFull) {
            result.error = "Zip64 archives are not supported";
            return result;
        }
        if (((entry.attributes >> 16) & 0xF000u) == 0xA000u) {
            result.error = "The archive contains a link, which is not supported";
            return result;
        }
        if (entry.size > options.maxEntryBytes) {
            result.error = "The archive is larger than allowed";
            return result;
        }
        totalSize += entry.size;
        if (totalSize > options.maxTotalBytes) {
            result.error = "The archive is larger than allowed";
            return result;
        }
        entries.push_back(std::move(entry));
    }

    unsigned long long done = 0;
    for (const Entry& entry : entries) {
        unsigned char local[30];
        in.clear();
        in.seekg(static_cast<std::streamoff>(entry.offset));
        in.read(reinterpret_cast<char*>(local), 30);
        if (!in || readLe32(local) != 0x04034B50ull) {
            result.error = "The archive is damaged";
            return result;
        }
        const unsigned long long dataStart = entry.offset + 30 + readLe16(local + 26) + readLe16(local + 28);
        if (dataStart + entry.compressed > fileSize) {
            result.error = "The archive is damaged";
            return result;
        }
        fs::path target;
        if (!prepareTarget(destination, entry.relative, target, result.error)) {
            return result;
        }
        std::error_code ec;
        fs::remove(target, ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = "The file cannot be created: " + target.string();
            return result;
        }
        in.seekg(static_cast<std::streamoff>(dataStart));
        unsigned long long remaining = entry.compressed;
        unsigned int crc = 0;
        unsigned long long written = 0;
        bool writeOk = true;
        bool cancelled = false;
        auto sink = [&](const unsigned char* data, size_t size) {
            out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!out) {
                writeOk = false;
                return false;
            }
            crc = crc32Update(crc, data, size);
            written += size;
            done += size;
            if (options.progress && !options.progress(done, totalSize)) {
                cancelled = true;
                return false;
            }
            return true;
        };
        bool ok = true;
        std::string error;
        if (entry.method == 0) {
            std::vector<unsigned char> buffer(256 * 1024);
            while (remaining > 0 && ok) {
                const size_t take = static_cast<size_t>(std::min<unsigned long long>(remaining, buffer.size()));
                in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(take));
                if (!in) {
                    error = "The archive cannot be read";
                    ok = false;
                    break;
                }
                remaining -= take;
                ok = sink(buffer.data(), take);
            }
        } else {
            Inflater inflater(
                [&](unsigned char* buffer, size_t capacity) -> size_t {
                    const size_t take = static_cast<size_t>(std::min<unsigned long long>(remaining, capacity));
                    if (take == 0) {
                        return 0;
                    }
                    in.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(take));
                    if (!in) {
                        return 0;
                    }
                    remaining -= take;
                    return take;
                },
                sink, entry.size);
            ok = inflater.run(error);
        }
        out.close();
        if (cancelled) {
            result.cancelled = true;
            result.error = "Cancelled";
            return result;
        }
        if (!writeOk) {
            result.error = "The file cannot be written (disk full?): " + target.string();
            return result;
        }
        if (!ok) {
            result.error = error.empty() ? "The archive cannot be unpacked" : error;
            return result;
        }
        if (written != entry.size || crc != entry.crc) {
            result.error = "The file is damaged (checksum mismatch): " + entry.relative;
            return result;
        }
        result.bytes += written;
        result.files.push_back(entry.relative);
    }
    result.ok = true;
    return result;
}

Result extractTar(const fs::path& archive, const fs::path& destination, const Options& options) {
    Result result;
    std::ifstream in(archive, std::ios::binary);
    if (!in) {
        result.error = "The archive cannot be opened";
        return result;
    }
    in.seekg(0, std::ios::end);
    const unsigned long long fileSize = static_cast<unsigned long long>(in.tellg());
    in.seekg(0);
    TarReader reader(destination, options, result);
    std::vector<unsigned char> buffer(256 * 1024);
    unsigned long long done = 0;
    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const size_t got = static_cast<size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        if (!reader.feed(buffer.data(), got)) {
            return result;
        }
        done += got;
        if (options.progress && !options.progress(done, fileSize)) {
            result.cancelled = true;
            result.error = "Cancelled";
            return result;
        }
    }
    if (!reader.finish()) {
        return result;
    }
    result.ok = true;
    return result;
}

Result extractTarXz(const fs::path& archive, const fs::path& destination, const Options& options) {
    Result result;
#ifdef _WIN32
    (void)archive;
    (void)destination;
    (void)options;
    result.error = "The tar.xz format is not supported on this system";
    return result;
#else
    LzmaApi api;
    if (!loadLzma(api)) {
        result.error = "The xz library (liblzma.so.5) was not found on this computer";
        return result;
    }
    std::ifstream in(archive, std::ios::binary);
    if (!in) {
        dlclose(api.handle);
        result.error = "The archive cannot be opened";
        return result;
    }
    in.seekg(0, std::ios::end);
    const unsigned long long fileSize = static_cast<unsigned long long>(in.tellg());
    in.seekg(0);
    LzmaStream stream;
    if (api.decoder(&stream, ~0ull, 0) != 0) {
        dlclose(api.handle);
        result.error = "The xz decoder could not be started";
        return result;
    }
    TarReader reader(destination, options, result);
    std::vector<unsigned char> inputBuffer(256 * 1024);
    std::vector<unsigned char> outputBuffer(256 * 1024);
    bool inputDone = false;
    bool streamEnd = false;
    bool failed = false;
    unsigned long long consumed = 0;
    while (!streamEnd && !failed) {
        if (stream.availIn == 0 && !inputDone) {
            in.read(reinterpret_cast<char*>(inputBuffer.data()), static_cast<std::streamsize>(inputBuffer.size()));
            const size_t got = static_cast<size_t>(in.gcount());
            consumed += got;
            if (got == 0) {
                inputDone = true;
            }
            stream.nextIn = inputBuffer.data();
            stream.availIn = got;
        }
        stream.nextOut = outputBuffer.data();
        stream.availOut = outputBuffer.size();
        const int code = api.code(&stream, inputDone ? 3 : 0);
        const size_t produced = outputBuffer.size() - stream.availOut;
        if (produced > 0) {
            if (stream.totalOut > kMaxDecodedBytes) {
                result.error = "The archive is larger than allowed";
                failed = true;
                break;
            }
            if (!reader.feed(outputBuffer.data(), produced)) {
                failed = true;
                break;
            }
        }
        if (code == 1) {
            streamEnd = true;
        } else if (code != 0) {
            result.error = code == 9 || code == 7 ? "The archive is damaged" : "The archive cannot be unpacked";
            failed = true;
            break;
        }
        if (options.progress && !options.progress(consumed, fileSize)) {
            result.cancelled = true;
            result.error = "Cancelled";
            failed = true;
        }
    }
    api.end(&stream);
    dlclose(api.handle);
    if (failed) {
        reader.finish();
        return result;
    }
    if (!reader.finish()) {
        return result;
    }
    result.ok = true;
    return result;
#endif
}

}
