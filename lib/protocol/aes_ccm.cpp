/**
 * @file aes_ccm.cpp
 * @brief AES-128-CCM tag for AesCcmAuth. The key stays in the object.
 *
 * The frame is associated data. There is no encrypted payload, so the only
 * ciphertext is the 8-byte tag. verify() compares that tag and does not
 * touch the replay store.
 */

#include "security.h"

#include <cstring>

namespace proto {
namespace {

constexpr size_t kBlockBytes = 16;
constexpr size_t kRoundKeyBytes = 176;
constexpr size_t kHeaderBytesForNonce = 11;
constexpr size_t kSrcOffset = 2;
constexpr size_t kCounterOffset = 6;

/** @brief AES S-box. */
constexpr uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

/** @brief Round constants. Index 1 is the first expanded round. */
constexpr uint8_t kRcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

/** @brief Multiply by x in AES's field. */
uint8_t xtime(uint8_t value) {
    return static_cast<uint8_t>((value << 1) ^ ((value & 0x80) != 0 ? 0x1b : 0));
}

/** @brief Expand a 16-byte key into 11 round keys. */
void expandKey(const uint8_t key[AesCcmAuth::kKeyBytes], uint8_t roundKeys[kRoundKeyBytes]) {
    std::memcpy(roundKeys, key, AesCcmAuth::kKeyBytes);
    size_t produced = AesCcmAuth::kKeyBytes;
    uint8_t rconIndex = 1;
    uint8_t word[4];
    while (produced < kRoundKeyBytes) {
        word[0] = roundKeys[produced - 4];
        word[1] = roundKeys[produced - 3];
        word[2] = roundKeys[produced - 2];
        word[3] = roundKeys[produced - 1];
        if (produced % AesCcmAuth::kKeyBytes == 0) {
            const uint8_t first = word[0];
            word[0] = static_cast<uint8_t>(kSbox[word[1]] ^ kRcon[rconIndex]);
            word[1] = kSbox[word[2]];
            word[2] = kSbox[word[3]];
            word[3] = kSbox[first];
            rconIndex = static_cast<uint8_t>(rconIndex + 1);
        }
        for (size_t i = 0; i < 4; ++i) {
            roundKeys[produced] = static_cast<uint8_t>(roundKeys[produced - AesCcmAuth::kKeyBytes] ^ word[i]);
            produced += 1;
        }
    }
}

/** @brief Encrypt one block. @p roundKeys is the output of expandKey(). */
void encryptBlock(const uint8_t roundKeys[kRoundKeyBytes], const uint8_t in[kBlockBytes], uint8_t out[kBlockBytes]) {
    uint8_t state[kBlockBytes];
    std::memcpy(state, in, kBlockBytes);
    for (size_t i = 0; i < kBlockBytes; ++i) {
        state[i] = static_cast<uint8_t>(state[i] ^ roundKeys[i]);
    }
    for (size_t round = 1; round <= 9; ++round) {
        for (size_t i = 0; i < kBlockBytes; ++i) {
            state[i] = kSbox[state[i]];
        }
        const uint8_t row1 = state[1];
        state[1] = state[5];
        state[5] = state[9];
        state[9] = state[13];
        state[13] = row1;
        const uint8_t row2a = state[2];
        const uint8_t row2b = state[6];
        state[2] = state[10];
        state[6] = state[14];
        state[10] = row2a;
        state[14] = row2b;
        const uint8_t row3 = state[15];
        state[15] = state[11];
        state[11] = state[7];
        state[7] = state[3];
        state[3] = row3;
        for (size_t column = 0; column < 4; ++column) {
            uint8_t* p = state + column * 4;
            const uint8_t a0 = p[0];
            const uint8_t a1 = p[1];
            const uint8_t a2 = p[2];
            const uint8_t a3 = p[3];
            p[0] = static_cast<uint8_t>(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
            p[1] = static_cast<uint8_t>(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
            p[2] = static_cast<uint8_t>(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
            p[3] = static_cast<uint8_t>(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
        }
        const uint8_t* roundKey = roundKeys + round * kBlockBytes;
        for (size_t i = 0; i < kBlockBytes; ++i) {
            state[i] = static_cast<uint8_t>(state[i] ^ roundKey[i]);
        }
    }
    for (size_t i = 0; i < kBlockBytes; ++i) {
        state[i] = kSbox[state[i]];
    }
    const uint8_t row1 = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = row1;
    const uint8_t row2a = state[2];
    const uint8_t row2b = state[6];
    state[2] = state[10];
    state[6] = state[14];
    state[10] = row2a;
    state[14] = row2b;
    const uint8_t row3 = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = row3;
    const uint8_t* lastKey = roundKeys + 10 * kBlockBytes;
    for (size_t i = 0; i < kBlockBytes; ++i) {
        out[i] = static_cast<uint8_t>(state[i] ^ lastKey[i]);
    }
}

/**
 * @brief Fill a 13-byte nonce from the sender id and the counter in @p frame.
 * @p frame must hold a header.
 */
void buildNonce(const uint8_t* frame, uint8_t nonce[AesCcmAuth::kNonceBytes]) {
    std::memset(nonce, 0, AesCcmAuth::kNonceBytes);
    nonce[0] = frame[kSrcOffset];
    nonce[1] = frame[kCounterOffset];
    nonce[2] = frame[kCounterOffset + 1];
    nonce[3] = frame[kCounterOffset + 2];
    nonce[4] = frame[kCounterOffset + 3];
}

/**
 * @brief AES-CCM tag for associated data and an empty payload.
 * @param tag Receives the first 8 bytes of the encrypted CBC-MAC.
 */
void ccmTag(const uint8_t key[AesCcmAuth::kKeyBytes],
            const uint8_t nonce[AesCcmAuth::kNonceBytes],
            const uint8_t* frame,
            size_t frameLen,
            uint8_t tag[kAuthTagBytes]) {
    uint8_t roundKeys[kRoundKeyBytes];
    expandKey(key, roundKeys);

    uint8_t block[kBlockBytes] = {};
    block[0] = 0x59;
    std::memcpy(block + 1, nonce, AesCcmAuth::kNonceBytes);
    uint8_t mac[kBlockBytes];
    encryptBlock(roundKeys, block, mac);

    size_t consumed = 0;
    while (consumed < frameLen) {
        std::memset(block, 0, kBlockBytes);
        size_t pos = 0;
        if (consumed == 0) {
            block[0] = static_cast<uint8_t>(frameLen >> 8);
            block[1] = static_cast<uint8_t>(frameLen);
            pos = 2;
        }
        while (pos < kBlockBytes && consumed < frameLen) {
            block[pos] = frame[consumed];
            pos += 1;
            consumed += 1;
        }
        for (size_t i = 0; i < kBlockBytes; ++i) {
            block[i] = static_cast<uint8_t>(block[i] ^ mac[i]);
        }
        encryptBlock(roundKeys, block, mac);
    }

    std::memset(block, 0, kBlockBytes);
    block[0] = 0x01;
    std::memcpy(block + 1, nonce, AesCcmAuth::kNonceBytes);
    uint8_t pad[kBlockBytes];
    encryptBlock(roundKeys, block, pad);
    for (size_t i = 0; i < kAuthTagBytes; ++i) {
        tag[i] = static_cast<uint8_t>(mac[i] ^ pad[i]);
    }
}

/** @brief True when every tag byte matches. The loop does not stop at the first difference. */
bool tagsMatch(const uint8_t* left, const uint8_t* right) {
    uint8_t diff = 0;
    for (size_t i = 0; i < kAuthTagBytes; ++i) {
        diff = static_cast<uint8_t>(diff | (left[i] ^ right[i]));
    }
    return diff == 0;
}

}  // namespace

AesCcmAuth::AesCcmAuth(const uint8_t* key) {
    if (key == nullptr) {
        return;
    }
    std::memcpy(key_, key, kKeyBytes);
    keyOk_ = true;
}

uint8_t AesCcmAuth::tagLen() const {
    return kAuthTagBytes;
}

Status AesCcmAuth::sign(const uint8_t* frame, size_t frameLen, uint8_t* tag) const {
    if (!keyOk_ || frame == nullptr || tag == nullptr || frameLen < kHeaderBytesForNonce) {
        return Status::BadPayload;
    }
    uint8_t nonce[kNonceBytes];
    buildNonce(frame, nonce);
    ccmTag(key_, nonce, frame, frameLen, tag);
    return Status::Ok;
}

Status AesCcmAuth::verify(const uint8_t* frame, size_t frameLen, const uint8_t* tag) const {
    if (!keyOk_ || frame == nullptr || tag == nullptr || frameLen < kHeaderBytesForNonce) {
        return Status::BadPayload;
    }
    uint8_t nonce[kNonceBytes];
    buildNonce(frame, nonce);
    uint8_t expected[kAuthTagBytes];
    ccmTag(key_, nonce, frame, frameLen, expected);
    if (!tagsMatch(expected, tag)) {
        return Status::BadAuth;
    }
    return Status::Ok;
}

}  // namespace proto
