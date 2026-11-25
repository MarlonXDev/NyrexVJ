#include "control/netsync_key.hpp"

#include "control/api_security.hpp"
#include "core/log.hpp"
#include "core/sha256.hpp"
#include "platform/platform.hpp"

#include <cctype>
#include <filesystem>

namespace nvj::netsync {

std::string keyPath() {
    return (std::filesystem::path(userDataDir()) / "netsync_key").string();
}

bool generateKey(Key& key) {
    Key fresh{};
    if (!platform::secureRandomBytes(fresh.data(), fresh.size())) {
        return false;
    }
    if (keyIsZero(fresh)) {
        return false;
    }
    key = fresh;
    return true;
}

bool saveKey(const Key& key) {
    if (keyIsZero(key)) {
        return false;
    }
    return api::saveSecretFile(keyPath(), keyToHex(key));
}

bool loadKey(Key& key) {
    std::string text;
    if (!api::loadSecretFile(keyPath(), text)) {
        return false;
    }
    Key parsed{};
    if (!keyFromHex(text, parsed) || keyIsZero(parsed)) {
        return false;
    }
    key = parsed;
    return true;
}

bool ensureKey(Key& key, bool& created) {
    created = false;
    if (loadKey(key)) {
        return true;
    }
    if (!generateKey(key)) {
        return false;
    }
    created = true;
    return true;
}

bool importKey(const std::string& text, Key& key) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            cleaned.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    Key parsed{};
    if (!keyFromHex(cleaned, parsed) || keyIsZero(parsed)) {
        return false;
    }
    key = parsed;
    return true;
}

std::string keyFingerprint(const Key& key) {
    if (keyIsZero(key)) {
        return std::string();
    }
    static const char label[] = "nyrexvj-netsync-fingerprint";
    Sha256 hash;
    hash.update(reinterpret_cast<const unsigned char*>(label), sizeof(label) - 1);
    hash.update(key.data(), key.size());
    return hash.finalHex().substr(0, 8);
}

}
