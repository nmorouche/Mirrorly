// Mirrorly - crypto helpers built on Monocypher (Ed25519/X25519/SHA-512) and tiny-AES
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mn {

void sha512(const uint8_t* data, size_t len, uint8_t out[64]);
// sha512(a || b)
void sha512_2(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen, uint8_t out[64]);

// Continuous AES-128-CTR keystream (state is kept across calls).
class AesCtr {
public:
    AesCtr();
    ~AesCtr();
    void init(const uint8_t key[16], const uint8_t iv[16]);
    void process(uint8_t* buf, size_t len);  // in place encrypt == decrypt
private:
    void* ctx_;
    uint8_t counter_[16];
    uint8_t stream_[16];
    int used_;
};

// AES-128-CBC decrypt in place; len must be a multiple of 16.
void aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* buf, size_t len);
void aes_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* buf, size_t len);

struct Ed25519Key {
    uint8_t seed[32];
    uint8_t secret[64];  // monocypher format: seed || public
    uint8_t pub[32];
    void fromSeed(const uint8_t s[32]);
    void sign(const uint8_t* msg, size_t len, uint8_t sig[64]) const;
};
bool ed25519_verify(const uint8_t pub[32], const uint8_t* msg, size_t len, const uint8_t sig[64]);

void x25519_keypair(uint8_t secret[32], uint8_t pub[32]);
void x25519_shared(const uint8_t secret[32], const uint8_t their_pub[32], uint8_t shared[32]);

}  // namespace mn
