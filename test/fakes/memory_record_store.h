/**
 * @file memory_record_store.h
 * @brief RAM RecordStorePort for host tests.
 */

#pragma once

#include <record_store_port.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * @brief Fixed table of blobs kept in RAM.
 *
 * The first successful commit for a key fixes that blob's size. A later commit
 * with a different size fails and leaves the stored bytes unchanged.
 */
class MemoryRecordStore final : public RecordStorePort {
public:
    /** @brief Largest blob commit() will store. */
    static constexpr size_t kMaxBlobBytes = 16;
    /** @brief Number of distinct keys the table can hold. */
    static constexpr size_t kCapacity = 64;

    /**
     * @brief Copy the blob at @p key when its stored size is @p len.
     * @return false when the key is absent, the size differs, or @p dst is null.
     */
    bool read(uint16_t key, uint8_t* dst, size_t len) override {
        const Slot* slot = find(key);
        if (slot == nullptr || dst == nullptr || slot->len != len) {
            return false;
        }
        std::memcpy(dst, slot->data, len);
        return true;
    }

    /**
     * @brief Insert or replace the blob at @p key.
     *
     * Returns false, without writing, when @p src is null, @p len is 0 or greater
     * than kMaxBlobBytes, the key already has a different size, or the table is full.
     */
    bool commit(uint16_t key, const uint8_t* src, size_t len) override {
        if (src == nullptr || len == 0 || len > kMaxBlobBytes) {
            return false;
        }
        Slot* slot = find(key);
        if (slot != nullptr) {
            if (slot->len != len) {
                return false;
            }
            std::memcpy(slot->data, src, len);
            return true;
        }
        for (Slot& candidate : slots_) {
            if (!candidate.used) {
                candidate.used = true;
                candidate.key = key;
                candidate.len = static_cast<uint8_t>(len);
                std::memcpy(candidate.data, src, len);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Drop the blob at @p key.
     * @return true when the key is already absent or was removed.
     */
    bool erase(uint16_t key) override {
        Slot* slot = find(key);
        if (slot == nullptr) {
            return true;
        }
        slot->used = false;
        slot->len = 0;
        return true;
    }

private:
    struct Slot {
        bool used = false;
        uint16_t key = 0;
        uint8_t len = 0;
        uint8_t data[kMaxBlobBytes] = {};
    };

    Slot* find(uint16_t key) {
        for (Slot& slot : slots_) {
            if (slot.used && slot.key == key) {
                return &slot;
            }
        }
        return nullptr;
    }

    const Slot* find(uint16_t key) const {
        for (const Slot& slot : slots_) {
            if (slot.used && slot.key == key) {
                return &slot;
            }
        }
        return nullptr;
    }

    Slot slots_[kCapacity] = {};
};
