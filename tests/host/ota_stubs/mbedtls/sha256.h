// SPDX-License-Identifier: MIT
#pragma once
#include <openssl/evp.h>

// Real SHA-256 for host transaction tests; firmware uses the pinned SDK's mbedTLS.
struct mbedtls_sha256_context {
    EVP_MD_CTX *ctx{};
};

inline void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->ctx = EVP_MD_CTX_new(); }

inline int mbedtls_sha256_starts(mbedtls_sha256_context *c, int) {
    return c->ctx && EVP_DigestInit_ex(c->ctx, EVP_sha256(), nullptr) == 1 ? 0 : -1;
}

inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *p, std::size_t n) {
    return EVP_DigestUpdate(c->ctx, p, n) == 1 ? 0 : -1;
}

inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char *out) {
    return EVP_DigestFinal_ex(c->ctx, out, nullptr) == 1 ? 0 : -1;
}

inline void mbedtls_sha256_free(mbedtls_sha256_context *c) { EVP_MD_CTX_free(c->ctx); }
