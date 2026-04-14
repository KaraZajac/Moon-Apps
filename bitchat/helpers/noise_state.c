// Noise_XX_25519_ChaChaPoly_SHA256 implementation
// Compatible with BitChat Android (noise-java) and iOS (CryptoKit)

#include "noise_state.h"
#include "../crypto/sha256.h"
#include "../crypto/chacha20poly1305.h"
#include "../crypto/memzero.h"
#include <furi.h>
#include <furi_hal.h>
#include <string.h>

#define TAG "Noise"

// Protocol name for hashing
static const char PROTOCOL_NAME[] = "Noise_XX_25519_ChaChaPoly_SHA256";

// ── HMAC-SHA256 ─────────────────────────────────────────────────────

static void hmac_sha256(const uint8_t* key, size_t key_len,
                        const uint8_t* data, size_t data_len,
                        uint8_t out[32]) {
    uint8_t ipad[64], opad[64];
    uint8_t key_block[64] = {0};

    if(key_len > 64) {
        sha256_Raw(key, key_len, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    for(int i = 0; i < 64; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    SHA256_CTX ctx;
    sha256_Init(&ctx);
    sha256_Update(&ctx, ipad, 64);
    sha256_Update(&ctx, data, data_len);
    uint8_t inner[32];
    sha256_Final(&ctx, inner);

    sha256_Init(&ctx);
    sha256_Update(&ctx, opad, 64);
    sha256_Update(&ctx, inner, 32);
    sha256_Final(&ctx, out);

    memzero(key_block, 64);
    memzero(ipad, 64);
    memzero(opad, 64);
    memzero(inner, 32);
}

// ── HKDF (2-output) ────────────────────────────────────────────────

static void hkdf2(const uint8_t ck[32], const uint8_t* ikm, size_t ikm_len,
                   uint8_t out1[32], uint8_t out2[32]) {
    uint8_t prk[32];
    hmac_sha256(ck, 32, ikm, ikm_len, prk);

    // T1 = HMAC(PRK, 0x01)
    uint8_t one = 0x01;
    hmac_sha256(prk, 32, &one, 1, out1);

    // T2 = HMAC(PRK, T1 || 0x02)
    uint8_t t1_02[33];
    memcpy(t1_02, out1, 32);
    t1_02[32] = 0x02;
    hmac_sha256(prk, 32, t1_02, 33, out2);

    memzero(prk, 32);
}

// ── Curve25519 DH ───────────────────────────────────────────────────

extern void curve25519_scalarmult(uint8_t* out, const uint8_t* scalar, const uint8_t* point);
extern void curve25519_scalarmult_basepoint(uint8_t* out, const uint8_t* scalar);

static void noise_dh(uint8_t out[32], const uint8_t priv[32], const uint8_t pub[32]) {
    curve25519_scalarmult(out, priv, pub);
}

static void noise_generate_keypair(uint8_t priv[32], uint8_t pub[32]) {
    furi_hal_random_fill_buf(priv, 32);
    priv[0] &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
    curve25519_scalarmult_basepoint(pub, priv);
}

// ── CipherState ─────────────────────────────────────────────────────

static void cipher_init(NoiseCipherState* cs) {
    memset(cs, 0, sizeof(NoiseCipherState));
}

static void cipher_init_key(NoiseCipherState* cs, const uint8_t key[32]) {
    memcpy(cs->k, key, 32);
    cs->n = 0;
    cs->has_key = true;
}

static bool cipher_encrypt(NoiseCipherState* cs, const uint8_t* ad, size_t ad_len,
                           uint8_t* buf, size_t len, uint8_t tag[16]) {
    if(!cs->has_key) return true; // no key = no encryption (per Noise spec)
    uint8_t nonce[12] = {0};
    // IETF nonce: 4 zero bytes + 8 bytes LE counter
    nonce[4] = (cs->n) & 0xFF;
    nonce[5] = (cs->n >> 8) & 0xFF;
    nonce[6] = (cs->n >> 16) & 0xFF;
    nonce[7] = (cs->n >> 24) & 0xFF;
    chacha20poly1305_encrypt(cs->k, nonce, ad, ad_len, buf, len, tag);
    cs->n++;
    return true;
}

static bool cipher_decrypt(NoiseCipherState* cs, const uint8_t* ad, size_t ad_len,
                           uint8_t* buf, size_t len, const uint8_t tag[16]) {
    if(!cs->has_key) return true;
    uint8_t nonce[12] = {0};
    nonce[4] = (cs->n) & 0xFF;
    nonce[5] = (cs->n >> 8) & 0xFF;
    nonce[6] = (cs->n >> 16) & 0xFF;
    nonce[7] = (cs->n >> 24) & 0xFF;
    bool ok = chacha20poly1305_decrypt(cs->k, nonce, ad, ad_len, buf, len, tag);
    if(ok) cs->n++;
    return ok;
}

// ── SymmetricState ──────────────────────────────────────────────────

static void ss_init(NoiseSymmetricState* ss) {
    // h = SHA-256(protocol_name) if len <= 32, else pad
    size_t name_len = strlen(PROTOCOL_NAME);
    if(name_len <= 32) {
        memset(ss->h, 0, 32);
        memcpy(ss->h, PROTOCOL_NAME, name_len);
    } else {
        sha256_Raw((const uint8_t*)PROTOCOL_NAME, name_len, ss->h);
    }
    memcpy(ss->ck, ss->h, 32);
    cipher_init(&ss->cs);
}

static void ss_mix_hash(NoiseSymmetricState* ss, const uint8_t* data, size_t len) {
    SHA256_CTX ctx;
    sha256_Init(&ctx);
    sha256_Update(&ctx, ss->h, 32);
    sha256_Update(&ctx, data, len);
    sha256_Final(&ctx, ss->h);
}

static void ss_mix_key(NoiseSymmetricState* ss, const uint8_t* ikm, size_t len) {
    uint8_t temp_k[32];
    hkdf2(ss->ck, ikm, len, ss->ck, temp_k);
    cipher_init_key(&ss->cs, temp_k);
    memzero(temp_k, 32);
}

static bool ss_encrypt_and_hash(NoiseSymmetricState* ss,
                                uint8_t* buf, size_t len, uint8_t tag[16]) {
    bool ok = cipher_encrypt(&ss->cs, ss->h, 32, buf, len, tag);
    if(!ok) return false;
    // Mix ciphertext+tag into h
    SHA256_CTX ctx;
    sha256_Init(&ctx);
    sha256_Update(&ctx, ss->h, 32);
    sha256_Update(&ctx, buf, len);
    sha256_Update(&ctx, tag, 16);
    sha256_Final(&ctx, ss->h);
    return true;
}

static bool ss_decrypt_and_hash(NoiseSymmetricState* ss,
                                uint8_t* buf, size_t len, const uint8_t tag[16]) {
    // Save ciphertext+tag for hash before decryption
    SHA256_CTX ctx;
    sha256_Init(&ctx);
    sha256_Update(&ctx, ss->h, 32);
    sha256_Update(&ctx, buf, len);
    sha256_Update(&ctx, tag, 16);
    uint8_t new_h[32];
    sha256_Final(&ctx, new_h);

    bool ok = cipher_decrypt(&ss->cs, ss->h, 32, buf, len, tag);
    if(ok) memcpy(ss->h, new_h, 32);
    return ok;
}

static void ss_split(NoiseSymmetricState* ss,
                     NoiseCipherState* c1, NoiseCipherState* c2) {
    uint8_t k1[32], k2[32];
    hkdf2(ss->ck, NULL, 0, k1, k2);
    cipher_init_key(c1, k1);
    cipher_init_key(c2, k2);
    memzero(k1, 32);
    memzero(k2, 32);
}

// ── Handshake ───────────────────────────────────────────────────────

void noise_handshake_init(
    NoiseHandshakeState* hs, NoiseRole role,
    const uint8_t s_priv[32], const uint8_t s_pub[32]) {

    memset(hs, 0, sizeof(NoiseHandshakeState));
    hs->role = role;
    memcpy(hs->s_priv, s_priv, 32);
    memcpy(hs->s_pub, s_pub, 32);
    hs->msg_idx = 0;
    hs->complete = false;

    ss_init(&hs->ss);
    // XX pattern has no pre-messages, so we just mix an empty prologue
}

uint16_t noise_handshake_write(NoiseHandshakeState* hs,
                               uint8_t* out_buf, uint16_t out_buf_sz) {
    uint16_t pos = 0;

    if(hs->role == NoiseRoleInitiator && hs->msg_idx == 0) {
        // Message 1: -> e
        if(out_buf_sz < NOISE_MSG1_SIZE) return 0;
        noise_generate_keypair(hs->e_priv, hs->e_pub);
        memcpy(&out_buf[pos], hs->e_pub, 32); pos += 32;
        ss_mix_hash(&hs->ss, hs->e_pub, 32);
        hs->msg_idx = 1;
        return pos;
    }

    if(hs->role == NoiseRoleResponder && hs->msg_idx == 1) {
        // Message 2: <- e, ee, s, es
        if(out_buf_sz < NOISE_MSG2_SIZE) return 0;

        // e
        noise_generate_keypair(hs->e_priv, hs->e_pub);
        memcpy(&out_buf[pos], hs->e_pub, 32); pos += 32;
        ss_mix_hash(&hs->ss, hs->e_pub, 32);

        // ee
        uint8_t dh_result[32];
        noise_dh(dh_result, hs->e_priv, hs->re);
        ss_mix_key(&hs->ss, dh_result, 32);
        memzero(dh_result, 32);

        // s (encrypted)
        memcpy(&out_buf[pos], hs->s_pub, 32);
        uint8_t tag[16];
        ss_encrypt_and_hash(&hs->ss, &out_buf[pos], 32, tag);
        pos += 32;
        memcpy(&out_buf[pos], tag, 16); pos += 16;

        // es
        noise_dh(dh_result, hs->e_priv, hs->rs);
        ss_mix_key(&hs->ss, dh_result, 32);
        memzero(dh_result, 32);

        hs->msg_idx = 2;
        return pos;
    }

    if(hs->role == NoiseRoleInitiator && hs->msg_idx == 2) {
        // Message 3: -> s, se
        if(out_buf_sz < NOISE_MSG3_SIZE) return 0;

        // s (encrypted)
        memcpy(&out_buf[pos], hs->s_pub, 32);
        uint8_t tag[16];
        ss_encrypt_and_hash(&hs->ss, &out_buf[pos], 32, tag);
        pos += 32;
        memcpy(&out_buf[pos], tag, 16); pos += 16;

        // se
        uint8_t dh_result[32];
        noise_dh(dh_result, hs->s_priv, hs->re);
        ss_mix_key(&hs->ss, dh_result, 32);
        memzero(dh_result, 32);

        hs->complete = true;
        hs->msg_idx = 3;
        return pos;
    }

    return 0;
}

bool noise_handshake_read(NoiseHandshakeState* hs,
                          const uint8_t* msg, uint16_t msg_len) {

    if(hs->role == NoiseRoleResponder && hs->msg_idx == 0) {
        // Message 1: -> e
        if(msg_len < NOISE_MSG1_SIZE) return false;
        memcpy(hs->re, msg, 32);
        ss_mix_hash(&hs->ss, hs->re, 32);
        hs->msg_idx = 1;
        return true;
    }

    if(hs->role == NoiseRoleInitiator && hs->msg_idx == 1) {
        // Message 2: <- e, ee, s, es
        if(msg_len < NOISE_MSG2_SIZE) return false;
        uint16_t pos = 0;

        // e
        memcpy(hs->re, &msg[pos], 32); pos += 32;
        ss_mix_hash(&hs->ss, hs->re, 32);

        // ee
        uint8_t dh_result[32];
        noise_dh(dh_result, hs->e_priv, hs->re);
        ss_mix_key(&hs->ss, dh_result, 32);
        memzero(dh_result, 32);

        // s (encrypted: 32 bytes ciphertext + 16 bytes tag)
        uint8_t rs_buf[32];
        memcpy(rs_buf, &msg[pos], 32); pos += 32;
        const uint8_t* tag = &msg[pos]; pos += 16;
        if(!ss_decrypt_and_hash(&hs->ss, rs_buf, 32, tag)) return false;
        memcpy(hs->rs, rs_buf, 32);

        // es
        noise_dh(dh_result, hs->e_priv, hs->rs);
        ss_mix_key(&hs->ss, dh_result, 32);
        memzero(dh_result, 32);

        hs->msg_idx = 2;
        return true;
    }

    if(hs->role == NoiseRoleResponder && hs->msg_idx == 2) {
        // Message 3: -> s, se
        if(msg_len < NOISE_MSG3_SIZE) return false;
        uint16_t pos = 0;

        // s (encrypted)
        uint8_t rs_buf[32];
        memcpy(rs_buf, &msg[pos], 32); pos += 32;
        const uint8_t* tag = &msg[pos]; pos += 16;
        if(!ss_decrypt_and_hash(&hs->ss, rs_buf, 32, tag)) return false;
        memcpy(hs->rs, rs_buf, 32);

        // se
        uint8_t dh_result[32];
        noise_dh(dh_result, hs->e_priv, hs->rs);
        ss_mix_key(&hs->ss, dh_result, 32);
        memzero(dh_result, 32);

        hs->complete = true;
        hs->msg_idx = 3;
        return true;
    }

    return false;
}

bool noise_handshake_split(NoiseHandshakeState* hs, NoiseSession* session) {
    if(!hs->complete) return false;

    memset(session, 0, sizeof(NoiseSession));
    memcpy(session->remote_static, hs->rs, 32);

    if(hs->role == NoiseRoleInitiator) {
        ss_split(&hs->ss, &session->send_cipher, &session->recv_cipher);
    } else {
        ss_split(&hs->ss, &session->recv_cipher, &session->send_cipher);
    }

    session->active = true;
    session->created_tick = furi_get_tick();

    // Wipe handshake state
    memzero(hs->e_priv, 32);
    memzero(hs->s_priv, 32);

    FURI_LOG_I(TAG, "Handshake complete, session active");
    return true;
}

// ── Transport ───────────────────────────────────────────────────────

uint16_t noise_session_encrypt(NoiseSession* session,
                               const uint8_t* plaintext, uint16_t pt_len,
                               uint8_t* out_buf, uint16_t out_buf_sz) {
    if(!session->active) return 0;
    // Output: [4B nonce BE][ciphertext][16B tag]
    uint16_t needed = 4 + pt_len + NOISE_TAG_SIZE;
    if(needed > out_buf_sz) return 0;

    // Write nonce as big-endian 4 bytes
    uint32_t n = session->send_cipher.n;
    out_buf[0] = (n >> 24) & 0xFF;
    out_buf[1] = (n >> 16) & 0xFF;
    out_buf[2] = (n >> 8) & 0xFF;
    out_buf[3] = n & 0xFF;

    // Copy plaintext to output area (after nonce)
    memcpy(&out_buf[4], plaintext, pt_len);

    // Encrypt in place
    uint8_t tag[16];
    uint8_t nonce[12] = {0};
    nonce[4] = (n) & 0xFF;
    nonce[5] = (n >> 8) & 0xFF;
    nonce[6] = (n >> 16) & 0xFF;
    nonce[7] = (n >> 24) & 0xFF;
    chacha20poly1305_encrypt(session->send_cipher.k, nonce, NULL, 0,
                             &out_buf[4], pt_len, tag);
    session->send_cipher.n++;

    // Append tag
    memcpy(&out_buf[4 + pt_len], tag, 16);

    return needed;
}

uint16_t noise_session_decrypt(NoiseSession* session,
                               const uint8_t* encrypted, uint16_t enc_len,
                               uint8_t* out_buf, uint16_t out_buf_sz) {
    if(!session->active) return 0;
    // Input: [4B nonce BE][ciphertext][16B tag]
    if(enc_len < 4 + NOISE_TAG_SIZE) return 0;

    uint16_t ct_len = enc_len - 4 - NOISE_TAG_SIZE;
    if(ct_len > out_buf_sz) return 0;

    // Read nonce
    uint32_t n = ((uint32_t)encrypted[0] << 24) | ((uint32_t)encrypted[1] << 16) |
                 ((uint32_t)encrypted[2] << 8) | encrypted[3];

    // Copy ciphertext to output
    memcpy(out_buf, &encrypted[4], ct_len);
    const uint8_t* tag = &encrypted[4 + ct_len];

    // Build IETF nonce
    uint8_t nonce[12] = {0};
    nonce[4] = (n) & 0xFF;
    nonce[5] = (n >> 8) & 0xFF;
    nonce[6] = (n >> 16) & 0xFF;
    nonce[7] = (n >> 24) & 0xFF;

    // Temporarily set the recv cipher's nonce to the received value
    uint32_t saved_n = session->recv_cipher.n;
    session->recv_cipher.n = n;

    if(!chacha20poly1305_decrypt(session->recv_cipher.k, nonce, NULL, 0,
                                 out_buf, ct_len, tag)) {
        session->recv_cipher.n = saved_n; // restore on failure
        return 0;
    }

    // Update nonce counter to max(current, received+1)
    if(n + 1 > session->recv_cipher.n) {
        session->recv_cipher.n = n + 1;
    } else {
        session->recv_cipher.n = saved_n; // keep higher value
    }

    return ct_len;
}
