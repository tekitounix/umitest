// SPDX-License-Identifier: MIT
// SHA-512 Hash Function Implementation (FIPS 180-4)

#include "sha512.hh"

namespace umi::crypto {

namespace {

// SHA-512 initial hash values (first 64 bits of fractional parts of square roots of first 8 primes)
constexpr std::array<uint64_t, 8> SHA512_INIT = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

// SHA-512 round constants (first 64 bits of fractional parts of cube roots of first 80 primes)
constexpr std::array<uint64_t, 80> K = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

// Rotate right
constexpr uint64_t rotr(uint64_t x, int n) noexcept {
    return (x >> n) | (x << (64 - n));
}

// SHA-512 functions
constexpr uint64_t Ch(uint64_t x, uint64_t y, uint64_t z) noexcept {
    return (x & y) ^ (~x & z);
}

constexpr uint64_t Maj(uint64_t x, uint64_t y, uint64_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr uint64_t Sigma0(uint64_t x) noexcept {
    return rotr(x, 28) ^ rotr(x, 34) ^ rotr(x, 39);
}

constexpr uint64_t Sigma1(uint64_t x) noexcept {
    return rotr(x, 14) ^ rotr(x, 18) ^ rotr(x, 41);
}

constexpr uint64_t sigma0(uint64_t x) noexcept {
    return rotr(x, 1) ^ rotr(x, 8) ^ (x >> 7);
}

constexpr uint64_t sigma1(uint64_t x) noexcept {
    return rotr(x, 19) ^ rotr(x, 61) ^ (x >> 6);
}

// Load big-endian 64-bit value
inline uint64_t load_be64(const uint8_t* p) noexcept {
    return (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8) | static_cast<uint64_t>(p[7]);
}

// Store big-endian 64-bit value
inline void store_be64(uint8_t* p, uint64_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 56);
    p[1] = static_cast<uint8_t>(v >> 48);
    p[2] = static_cast<uint8_t>(v >> 40);
    p[3] = static_cast<uint8_t>(v >> 32);
    p[4] = static_cast<uint8_t>(v >> 24);
    p[5] = static_cast<uint8_t>(v >> 16);
    p[6] = static_cast<uint8_t>(v >> 8);
    p[7] = static_cast<uint8_t>(v);
}

// Process one 128-byte block
void sha512_transform(std::array<uint64_t, 8>& state, const uint8_t* block) noexcept {
    std::array<uint64_t, 80> W;

    // Prepare message schedule
    for (int i = 0; i < 16; ++i) {
        W[i] = load_be64(block + i * 8);
    }
    for (int i = 16; i < 80; ++i) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    // Initialize working variables
    uint64_t a = state[0];
    uint64_t b = state[1];
    uint64_t c = state[2];
    uint64_t d = state[3];
    uint64_t e = state[4];
    uint64_t f = state[5];
    uint64_t g = state[6];
    uint64_t h = state[7];

    // Main loop
    for (int i = 0; i < 80; ++i) {
        uint64_t T1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];
        uint64_t T2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    // Update state
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

void sha512_init(Sha512Context& ctx) noexcept {
    ctx.state = SHA512_INIT;
    ctx.count_low = 0;
    ctx.count_high = 0;
    ctx.buffer_len = 0;
}

void sha512_update(Sha512Context& ctx, const uint8_t* data, size_t len) noexcept {
    // Update bit count
    uint64_t bit_len = static_cast<uint64_t>(len) << 3;
    uint64_t old_count = ctx.count_low;
    ctx.count_low += bit_len;
    if (ctx.count_low < old_count) {
        ctx.count_high++;
    }

    // Process data
    while (len > 0) {
        size_t copy_len = SHA512_BLOCK_SIZE - ctx.buffer_len;
        if (copy_len > len) {
            copy_len = len;
        }

        for (size_t i = 0; i < copy_len; ++i) {
            ctx.buffer[ctx.buffer_len + i] = data[i];
        }
        ctx.buffer_len += copy_len;
        data += copy_len;
        len -= copy_len;

        if (ctx.buffer_len == SHA512_BLOCK_SIZE) {
            sha512_transform(ctx.state, ctx.buffer.data());
            ctx.buffer_len = 0;
        }
    }
}

void sha512_final(Sha512Context& ctx, uint8_t* hash) noexcept {
    // Pad message
    ctx.buffer[ctx.buffer_len++] = 0x80;

    // If not enough room for length, process current block and start new one
    if (ctx.buffer_len > 112) {
        while (ctx.buffer_len < SHA512_BLOCK_SIZE) {
            ctx.buffer[ctx.buffer_len++] = 0;
        }
        sha512_transform(ctx.state, ctx.buffer.data());
        ctx.buffer_len = 0;
    }

    // Pad to 112 bytes
    while (ctx.buffer_len < 112) {
        ctx.buffer[ctx.buffer_len++] = 0;
    }

    // Append bit length (128 bits, big-endian)
    store_be64(ctx.buffer.data() + 112, ctx.count_high);
    store_be64(ctx.buffer.data() + 120, ctx.count_low);
    sha512_transform(ctx.state, ctx.buffer.data());

    // Output hash
    for (int i = 0; i < 8; ++i) {
        store_be64(hash + i * 8, ctx.state[i]);
    }
}

void sha512(const uint8_t* data, size_t len, uint8_t* hash) noexcept {
    Sha512Context ctx;
    sha512_init(ctx);
    sha512_update(ctx, data, len);
    sha512_final(ctx, hash);
}

} // namespace umi::crypto
