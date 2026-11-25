#pragma once

#include "core/netsync.hpp"

#include <string>

namespace nvj::netsync {

std::string keyPath();
bool generateKey(Key& key);
bool saveKey(const Key& key);
bool loadKey(Key& key);
bool ensureKey(Key& key, bool& created);
bool importKey(const std::string& text, Key& key);
std::string keyFingerprint(const Key& key);

}
