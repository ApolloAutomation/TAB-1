// SHA-256 and HMAC-SHA256 for the dashboard engine (FIPS 180-4, RFC 2104).
// Plain C++17, no platform code, so the host tests and the tablet compute the
// same bytes. Used for the layout hash, pairing, signed calls and input.
#ifndef TAB_CRYPTO_H
#define TAB_CRYPTO_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tab {
namespace crypto {

struct Sha256 {
  uint32_t h[8];
  uint64_t bits;
  uint8_t buf[64];
  size_t used;
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline void sha256_block(Sha256 &s, const uint8_t *p) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 | (uint32_t) p[4 * i + 2] << 8 |
           (uint32_t) p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3], e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
  for (int i = 0; i < 64; i++) {
    const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  s.h[0] += a;
  s.h[1] += b;
  s.h[2] += c;
  s.h[3] += d;
  s.h[4] += e;
  s.h[5] += f;
  s.h[6] += g;
  s.h[7] += h;
}

inline void sha256_init(Sha256 &s) {
  static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(s.h, H0, sizeof H0);
  s.bits = 0;
  s.used = 0;
}

inline void sha256_update(Sha256 &s, const void *data, size_t n) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  s.bits += (uint64_t) n * 8;
  if (s.used) {
    const size_t take = n < 64 - s.used ? n : 64 - s.used;
    memcpy(s.buf + s.used, p, take);
    s.used += take;
    p += take;
    n -= take;
    if (s.used < 64) return;
    sha256_block(s, s.buf);
    s.used = 0;
  }
  while (n >= 64) {
    sha256_block(s, p);
    p += 64;
    n -= 64;
  }
  if (n) {
    memcpy(s.buf, p, n);
    s.used = n;
  }
}

inline void sha256_final(Sha256 &s, uint8_t out[32]) {
  const uint64_t bits = s.bits;
  const uint8_t one = 0x80, zero = 0;
  sha256_update(s, &one, 1);
  while (s.used != 56) sha256_update(s, &zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; i++) len[i] = (uint8_t) (bits >> (56 - 8 * i));
  sha256_update(s, len, 8);
  for (int i = 0; i < 8; i++) {
    out[4 * i] = (uint8_t) (s.h[i] >> 24);
    out[4 * i + 1] = (uint8_t) (s.h[i] >> 16);
    out[4 * i + 2] = (uint8_t) (s.h[i] >> 8);
    out[4 * i + 3] = (uint8_t) s.h[i];
  }
}

inline void sha256(const void *data, size_t n, uint8_t out[32]) {
  Sha256 s;
  sha256_init(s);
  sha256_update(s, data, n);
  sha256_final(s, out);
}

// HMAC-SHA256 over a message given in parts: init, update as often as
// needed, final.
struct Hmac {
  Sha256 inner;
  uint8_t okey[64];
};

inline void hmac_init(Hmac &m, const uint8_t *key, size_t key_len) {
  uint8_t k[64];
  memset(k, 0, sizeof k);
  if (key_len > 64) {
    sha256(key, key_len, k);
  } else {
    memcpy(k, key, key_len);
  }
  uint8_t ikey[64];
  for (int i = 0; i < 64; i++) {
    ikey[i] = k[i] ^ 0x36;
    m.okey[i] = k[i] ^ 0x5c;
  }
  sha256_init(m.inner);
  sha256_update(m.inner, ikey, 64);
  memset(k, 0, sizeof k);
  memset(ikey, 0, sizeof ikey);
}

inline void hmac_update(Hmac &m, const void *data, size_t n) { sha256_update(m.inner, data, n); }

inline void hmac_update_str(Hmac &m, const char *text) { sha256_update(m.inner, text, strlen(text)); }

inline void hmac_final(Hmac &m, uint8_t out[32]) {
  uint8_t ih[32];
  sha256_final(m.inner, ih);
  Sha256 o;
  sha256_init(o);
  sha256_update(o, m.okey, 64);
  sha256_update(o, ih, 32);
  sha256_final(o, out);
  memset(m.okey, 0, sizeof m.okey);
}

inline void hmac_sha256(const uint8_t *key, size_t key_len, const void *msg, size_t n, uint8_t out[32]) {
  Hmac m;
  hmac_init(m, key, key_len);
  hmac_update(m, msg, n);
  hmac_final(m, out);
}

// Writes 2*n lower-case hex characters (no NUL).
inline void to_hex(const uint8_t *in, size_t n, char *out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 15];
  }
}

// True when text is exactly n lower-case hex characters.
inline bool is_hex(const char *text, size_t len, size_t n) {
  if (len != n) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// Reads 2*n hex characters (already checked with is_hex) into n bytes.
inline void from_hex(const char *text, size_t n, uint8_t *out) {
  for (size_t i = 0; i < n; i++) {
    uint8_t v = 0;
    for (int j = 0; j < 2; j++) {
      const char c = text[2 * i + j];
      v = (uint8_t) (v << 4 | (c <= '9' ? c - '0' : c - 'a' + 10));
    }
    out[i] = v;
  }
}

// Constant-time comparison of two byte strings of the same length.
inline bool equal(const void *a, const void *b, size_t n) {
  const uint8_t *x = static_cast<const uint8_t *>(a);
  const uint8_t *y = static_cast<const uint8_t *>(b);
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t) (x[i] ^ y[i]);
  return diff == 0;
}

}  // namespace crypto
}  // namespace tab

#endif  // TAB_CRYPTO_H
