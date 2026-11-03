/**
 * @file preferences_record_store.cpp
 * @brief ESP32 NVS implementation of RecordStorePort.
 *
 * Keys are four hex characters so they fit the 15-character NVS name limit.
 * The namespace is never cleared here.
 */

#include "preferences_record_store.h"

namespace {

constexpr char kNamespace[] = "lpstore";
constexpr char kHex[] = "0123456789ABCDEF";

}  // namespace

PreferencesRecordStore::~PreferencesRecordStore() {
    if (open_) {
        prefs_.end();
    }
}

bool PreferencesRecordStore::open() {
    if (open_) {
        return true;
    }
    open_ = prefs_.begin(kNamespace, false);
    return open_;
}

void PreferencesRecordStore::formatKey(uint16_t key, char out[5]) {
    out[0] = kHex[(key >> 12) & 0x0F];
    out[1] = kHex[(key >> 8) & 0x0F];
    out[2] = kHex[(key >> 4) & 0x0F];
    out[3] = kHex[key & 0x0F];
    out[4] = '\0';
}

bool PreferencesRecordStore::read(uint16_t key, uint8_t* dst, size_t len) {
    if (!open_ || dst == nullptr || len == 0 || len > kMaxBlobBytes) {
        return false;
    }
    char name[5];
    formatKey(key, name);
    if (!prefs_.isKey(name) || prefs_.getBytesLength(name) != len) {
        return false;
    }
    return prefs_.getBytes(name, dst, len) == len;
}

bool PreferencesRecordStore::commit(uint16_t key, const uint8_t* src, size_t len) {
    if (!open_ || src == nullptr || len == 0 || len > kMaxBlobBytes) {
        return false;
    }
    char name[5];
    formatKey(key, name);
    if (prefs_.isKey(name) && prefs_.getBytesLength(name) != len) {
        return false;
    }
    return prefs_.putBytes(name, src, len) == len;
}

bool PreferencesRecordStore::erase(uint16_t key) {
    if (!open_) {
        return false;
    }
    char name[5];
    formatKey(key, name);
    if (!prefs_.isKey(name)) {
        return true;
    }
    return prefs_.remove(name);
}
