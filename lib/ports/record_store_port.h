/**
 * @file record_store_port.h
 * @brief Persistent blobs addressed by a uint16 key.
 */

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Read and replace fixed-size records.
 *
 * A failed commit leaves the previous blob readable. Callers use that to avoid
 * accepting work that did not reach storage. This header does not include Arduino.h.
 */
class RecordStorePort {
public:
    virtual ~RecordStorePort() = default;

    /**
     * @brief Copy the blob stored at @p key into @p dst.
     * @param key Record id.
     * @param dst Destination. Must hold @p len bytes.
     * @param len Expected blob size.
     * @return true when a blob of exactly @p len bytes was copied.
     */
    virtual bool read(uint16_t key, uint8_t* dst, size_t len) = 0;

    /**
     * @brief Replace the blob stored at @p key.
     * @param key Record id.
     * @param src Bytes to store. Must hold @p len bytes.
     * @param len Blob size.
     * @return true when the new bytes are readable. false leaves the previous blob unchanged.
     */
    virtual bool commit(uint16_t key, const uint8_t* src, size_t len) = 0;
};
