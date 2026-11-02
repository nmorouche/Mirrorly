#include "crypto.h"

#include <cstring>

#include "platform.h"

extern "C" {
#include "aes.h"
#include "monocypher-ed25519.h"
#include "monocypher.h"
}

namespace mn {

void sha512(const uint8_t* data, size_t len, uint8_t out[64]) { crypto_sha512(out, data, len); }

void sha512_2(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen, uint8_t out[64]) {
    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    crypto_sha512_update(&ctx, a, alen);
    crypto_sha512_update(&ctx, b, blen);
    crypto_sha512_final(&ctx, out);
}

AesCtr::AesCtr() : ctx_(new AES_ctx), used_(16) { memset(counter_, 0, 16); }
AesCtr::~AesCtr() { delete (AES_ctx*)ctx_; }

void AesCtr::init(const uint8_t key[16], const uint8_t iv[16]) {
    AES_init_ctx((AES_ctx*)ctx_, key);
    memcpy(counter_, iv, 16);
    used_ = 16;
}

void AesCtr::process(uint8_t* buf, size_t len) {
    AES_ctx* c = (AES_ctx*)ctx_;
    for (size_t i = 0; i < len; i++) {
        if (used_ == 16) {
            memcpy(stream_, counter_, 16);
            AES_ECB_encrypt(c, stream_);
            for (int k = 15; k >= 0; k--)
                if (++counter_[k] != 0) break;  // 128-bit big endian increment
            used_ = 0;
        }
        buf[i] ^= stream_[used_++];
    }
}

void aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* buf, size_t len) {
    AES_ctx c;
    AES_init_ctx_iv(&c, key, iv);
    AES_CBC_decrypt_buffer(&c, buf, len);
}

void aes_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* buf, size_t len) {
    AES_ctx c;
    AES_init_ctx_iv(&c, key, iv);
    AES_CBC_encrypt_buffer(&c, buf, len);
}

void Ed25519Key::fromSeed(const uint8_t s[32]) {
    memcpy(seed, s, 32);
    uint8_t tmp[32];
    memcpy(tmp, s, 32);  // key_pair wipes the seed argument
    crypto_ed25519_key_pair(secret, pub, tmp);
}

void Ed25519Key::sign(const uint8_t* msg, size_t len, uint8_t sig[64]) const {
    crypto_ed25519_sign(sig, secret, msg, len);
}

bool ed25519_verify(const uint8_t pub[32], const uint8_t* msg, size_t len, const uint8_t sig[64]) {
    return crypto_ed25519_check(sig, pub, msg, len) == 0;
}

void x25519_keypair(uint8_t secret[32], uint8_t pub[32]) {
    random_bytes(secret, 32);
    crypto_x25519_public_key(pub, secret);
}

void x25519_shared(const uint8_t secret[32], const uint8_t their_pub[32], uint8_t shared[32]) {
    crypto_x25519(shared, secret, their_pub);
}

}  // namespace mn
