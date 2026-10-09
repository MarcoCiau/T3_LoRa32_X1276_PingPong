/**
 * @file preferences_record_store.h
 * @brief RecordStorePort backed by ESP32 NVS through Preferences.
 *
 * Host tests use MemoryRecordStore. This adapter is compiled only into device firmware.
 */

#pragma once

#include <record_store_port.h>

#include <Preferences.h>

#include <cstddef>
#include <cstdint>

/**
 * @brief NVS blobs addressed by the same uint16 keys as the host store.
 *
 * open() must succeed before read or commit. A commit whose size differs from
 * the stored blob returns false and does not call putBytes. This class does not
 * erase the NVS namespace.
 */
class PreferencesRecordStore final : public RecordStorePort {
public:
    /** @brief Largest blob stored for one key. Matches MemoryRecordStore::kMaxBlobBytes. */
    static constexpr size_t kMaxBlobBytes = 16;

    PreferencesRecordStore() = default;
    ~PreferencesRecordStore() override;

    PreferencesRecordStore(const PreferencesRecordStore&) = delete;
    PreferencesRecordStore& operator=(const PreferencesRecordStore&) = delete;

    /**
     * @brief Open the NVS namespace.
     * @return true when the namespace is open. A second call returns true without reopening.
     */
    bool open();

    /** @copydoc RecordStorePort::read */
    bool read(uint16_t key, uint8_t* dst, size_t len) override;

    /**
     * @copydoc RecordStorePort::commit
     * @return false when the namespace is closed, the size is illegal, or NVS rejects the write.
     */
    bool commit(uint16_t key, const uint8_t* src, size_t len) override;

private:
    /** @brief Write @p key as four uppercase hex characters. @p out must hold 5 bytes. */
    static void formatKey(uint16_t key, char out[5]);

    Preferences prefs_;
    bool open_ = false;
};
