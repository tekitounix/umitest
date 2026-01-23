// SPDX-License-Identifier: MIT
// Ed25519 Signature Verification Implementation (RFC 8032)
// Minimal implementation based on TweetNaCl

#include "ed25519.hh"
#include "sha512.hh"

#include <cstring>

namespace umi::crypto {

namespace {

// Field element: 16 limbs of 16 bits each
using Gf = int64_t[16];

constexpr Gf gf0 = {0};

// Curve constant d = -121665/121666 mod p
constexpr Gf D = {
    0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
    0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203
};

// 2*d
constexpr Gf D2 = {
    0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406
};

// sqrt(-1) mod p
constexpr Gf SQRTM1 = {
    0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
    0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83
};

// Base point
constexpr Gf GY = {
    0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
    0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666
};

constexpr Gf GX = {
    0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
    0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169
};

void set25519(Gf r, const Gf a) {
    for (int i = 0; i < 16; ++i) {
        r[i] = a[i];
    }
}

void car25519(Gf o) {
    for (int i = 0; i < 16; ++i) {
        o[(i + 1) % 16] += (i < 15 ? 1 : 38) * (o[i] >> 16);
        o[i] &= 0xffff;
    }
}

void sel25519(Gf p, Gf q, int b) {
    int64_t c = ~(b - 1);
    for (int i = 0; i < 16; ++i) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(uint8_t* o, const Gf n) {
    Gf m, t;
    set25519(t, n);
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = static_cast<uint8_t>(t[i] & 0xff);
        o[2 * i + 1] = static_cast<uint8_t>(t[i] >> 8);
    }
}

void unpack25519(Gf o, const uint8_t* n) {
    for (int i = 0; i < 16; ++i) {
        o[i] = n[2 * i] + (static_cast<int64_t>(n[2 * i + 1]) << 8);
    }
    o[15] &= 0x7fff;
}

void A(Gf o, const Gf a, const Gf b) {
    for (int i = 0; i < 16; ++i) {
        o[i] = a[i] + b[i];
    }
}

void Z(Gf o, const Gf a, const Gf b) {
    for (int i = 0; i < 16; ++i) {
        o[i] = a[i] - b[i];
    }
}

void M(Gf o, const Gf a, const Gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; ++i) {
        t[i] = 0;
    }
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (int i = 0; i < 15; ++i) {
        t[i] += 38 * t[i + 16];
    }
    for (int i = 0; i < 16; ++i) {
        o[i] = t[i];
    }
    car25519(o);
    car25519(o);
}

void S(Gf o, const Gf a) {
    M(o, a, a);
}

void inv25519(Gf o, const Gf i) {
    Gf c;
    set25519(c, i);
    for (int a = 253; a >= 0; --a) {
        S(c, c);
        if (a != 2 && a != 4) {
            M(c, c, i);
        }
    }
    set25519(o, c);
}

void pow2523(Gf o, const Gf i) {
    Gf c;
    set25519(c, i);
    for (int a = 250; a >= 0; --a) {
        S(c, c);
        if (a != 1) {
            M(c, c, i);
        }
    }
    set25519(o, c);
}

int neq25519(const Gf a, const Gf b) {
    uint8_t c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    int r = 0;
    for (int i = 0; i < 32; ++i) {
        r |= c[i] ^ d[i];
    }
    return r;
}

uint8_t par25519(const Gf a) {
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

// Extended coordinates (X:Y:Z:T) where x=X/Z, y=Y/Z, xy=T/Z
struct GeP3 {
    Gf X, Y, Z, T;
};

void ge_add(GeP3& r, const GeP3& p, const GeP3& q) {
    Gf a, b, c, d, t, e, f, g, h;
    Z(a, p.Y, p.X);
    Z(t, q.Y, q.X);
    M(a, a, t);
    A(b, p.X, p.Y);
    A(t, q.X, q.Y);
    M(b, b, t);
    M(c, p.T, q.T);
    M(c, c, D2);
    M(d, p.Z, q.Z);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(r.X, e, f);
    M(r.Y, h, g);
    M(r.Z, g, f);
    M(r.T, e, h);
}

void ge_double(GeP3& r, const GeP3& p) {
    Gf a, b, c, t, e, f, g, h;
    S(a, p.X);
    S(b, p.Y);
    S(c, p.Z);
    A(c, c, c);
    A(h, a, b);
    A(t, p.X, p.Y);
    S(t, t);
    Z(e, h, t);
    Z(g, a, b);
    A(f, c, g);
    M(r.X, e, f);
    M(r.Y, h, g);
    M(r.Z, g, f);
    M(r.T, e, h);
}

int ge_frombytes_negate_vartime(GeP3& p, const uint8_t* s) {
    Gf u, v, v3, vxx, m_rootcheck, p_rootcheck, one;

    one[0] = 1;
    for (int i = 1; i < 16; ++i) {
        one[i] = 0;
    }

    unpack25519(p.Y, s);
    set25519(p.Z, one);
    S(u, p.Y);
    M(v, u, D);
    Z(u, u, p.Z);  // u = y^2 - 1
    A(v, v, p.Z);  // v = dy^2 + 1

    S(v3, v);
    M(v3, v3, v);  // v3 = v^3
    S(p.X, v3);
    M(p.X, p.X, v);
    M(p.X, p.X, u);  // x = uv^7

    pow2523(p.X, p.X);  // x = (uv^7)^((q-5)/8)
    M(p.X, p.X, v3);
    M(p.X, p.X, u);     // x = uv^3(uv^7)^((q-5)/8)

    S(vxx, p.X);
    M(vxx, vxx, v);
    Z(m_rootcheck, vxx, u);  // vx^2 - u
    A(p_rootcheck, vxx, u);  // vx^2 + u
    if (neq25519(m_rootcheck, gf0) != 0) {
        if (neq25519(p_rootcheck, gf0) != 0) {
            return -1;
        }
        M(p.X, p.X, SQRTM1);
    }

    if (par25519(p.X) == (s[31] >> 7)) {
        Z(p.X, gf0, p.X);
    }

    M(p.T, p.X, p.Y);
    return 0;
}

void ge_tobytes(uint8_t* s, const GeP3& h) {
    Gf recip, x, y;
    inv25519(recip, h.Z);
    M(x, h.X, recip);
    M(y, h.Y, recip);
    pack25519(s, y);
    s[31] ^= par25519(x) << 7;
}

void ge_double_scalarmult_vartime(GeP3& r, const uint8_t* a, const GeP3& A, const uint8_t* b) {
    // r = a*A + b*B where B is base point
    GeP3 base;
    set25519(base.X, GX);
    set25519(base.Y, GY);
    base.Z[0] = 1;
    for (int i = 1; i < 16; ++i) {
        base.Z[i] = 0;
    }
    M(base.T, base.X, base.Y);

    // Zero point
    r.X[0] = 0;
    r.Y[0] = 1;
    r.Z[0] = 1;
    r.T[0] = 0;
    for (int i = 1; i < 16; ++i) {
        r.X[i] = 0;
        r.Y[i] = 0;
        r.Z[i] = 0;
        r.T[i] = 0;
    }

    for (int i = 255; i >= 0; --i) {
        uint8_t aa = (a[i >> 3] >> (i & 7)) & 1;
        uint8_t bb = (b[i >> 3] >> (i & 7)) & 1;
        ge_double(r, r);
        if (aa != 0) {
            ge_add(r, r, A);
        }
        if (bb != 0) {
            ge_add(r, r, base);
        }
    }
}

// Scalar mod L
constexpr int64_t L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10
};

void modL(uint8_t* r, int64_t* x) {
    for (int i = 63; i >= 32; --i) {
        int64_t carry = 0;
        for (int j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[i - 12] += carry;
        x[i] = 0;
    }
    int64_t carry = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j) {
        x[j] -= carry * L[j];
    }
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<uint8_t>(x[i] & 255);
    }
}

void reduce(uint8_t* r) {
    int64_t x[64];
    for (int i = 0; i < 64; ++i) {
        x[i] = r[i];
    }
    for (int i = 0; i < 64; ++i) {
        r[i] = 0;
    }
    modL(r, x);
}

int crypto_sign_verify_detached(const uint8_t* sig, const uint8_t* m, size_t mlen, const uint8_t* pk) {
    uint8_t t[32], h[64];
    GeP3 A, R;

    if ((sig[63] & 240) != 0) {
        return -1;
    }
    if (ge_frombytes_negate_vartime(A, pk) != 0) {
        return -1;
    }

    Sha512Context ctx;
    sha512_init(ctx);
    sha512_update(ctx, sig, 32);
    sha512_update(ctx, pk, 32);
    sha512_update(ctx, m, mlen);
    sha512_final(ctx, h);
    reduce(h);

    ge_double_scalarmult_vartime(R, h, A, sig + 32);
    ge_tobytes(t, R);

    int diff = 0;
    for (int i = 0; i < 32; ++i) {
        diff |= t[i] ^ sig[i];
    }
    return diff != 0 ? -1 : 0;
}

} // namespace

bool ed25519_verify(const uint8_t* signature, const uint8_t* public_key,
                    const uint8_t* message, size_t message_len) noexcept {
    return crypto_sign_verify_detached(signature, message, message_len, public_key) == 0;
}

void ed25519_verify_init(Ed25519Context& ctx, const uint8_t* signature,
                         const uint8_t* public_key) noexcept {
    std::memcpy(ctx.signature.data(), signature, 64);
    std::memcpy(ctx.public_key.data(), public_key, 32);
    std::memcpy(ctx.prefix.data(), signature, 32);
    std::memcpy(ctx.prefix.data() + 32, public_key, 32);
    ctx.prefix_len = 64;
    ctx.initialized = true;
}

void ed25519_verify_update(Ed25519Context& ctx, const uint8_t* data, size_t len) noexcept {
    (void)ctx;
    (void)data;
    (void)len;
}

bool ed25519_verify_final(Ed25519Context& ctx) noexcept {
    (void)ctx;
    return false;
}

} // namespace umi::crypto
