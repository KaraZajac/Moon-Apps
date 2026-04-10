#include "bitchat_protocol.h"
#include <string.h>

// ── Helpers ──────────────────────────────────────────────────────────

static void put_u16_be(uint8_t* buf, uint16_t val) {
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

static void put_u64_be(uint8_t* buf, uint64_t val) {
    for(int i = 7; i >= 0; i--) {
        buf[i] = val & 0xFF;
        val >>= 8;
    }
}

static uint16_t get_u16_be(const uint8_t* buf) {
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint64_t get_u64_be(const uint8_t* buf) {
    uint64_t val = 0;
    for(int i = 0; i < 8; i++) val = (val << 8) | buf[i];
    return val;
}

static uint64_t bc_timestamp_now(void) {
    // Unix timestamp in milliseconds (Android uses System.currentTimeMillis())
    extern uint32_t furi_hal_rtc_get_timestamp(void);
    return (uint64_t)furi_hal_rtc_get_timestamp() * 1000ULL;
}

// ── Encoding ─────────────────────────────────────────────────────────

uint16_t bc_encode_header(
    uint8_t* buf,
    uint16_t buf_sz,
    uint8_t type,
    uint8_t ttl,
    uint8_t flags,
    const uint8_t* sender_id,
    const uint8_t* payload,
    uint16_t payload_len) {
    (void)payload;
    if(buf_sz < BC_HEADER_SIZE + BC_SENDER_ID_SIZE) return 0;

    buf[0] = BC_VERSION;
    buf[1] = type;
    buf[2] = ttl;
    put_u64_be(&buf[3], bc_timestamp_now());
    buf[11] = flags;
    put_u16_be(&buf[12], payload_len);
    memcpy(&buf[14], sender_id, BC_SENDER_ID_SIZE);

    return BC_HEADER_SIZE + BC_SENDER_ID_SIZE; // 22 bytes
}

uint16_t bc_encode_announce(
    uint8_t* buf,
    uint16_t buf_sz,
    const BcAnnounce* announce) {
    uint16_t pos = 0;

    // Nickname TLV
    uint8_t nick_len = strlen(announce->nickname);
    if(pos + 2 + nick_len > buf_sz) return 0;
    buf[pos++] = BC_TLV_NICKNAME;
    buf[pos++] = nick_len;
    memcpy(&buf[pos], announce->nickname, nick_len);
    pos += nick_len;

    // Noise public key TLV (optional)
    if(announce->has_noise_key) {
        if(pos + 2 + 32 > buf_sz) return 0;
        buf[pos++] = BC_TLV_NOISE_PUBKEY;
        buf[pos++] = 32;
        memcpy(&buf[pos], announce->noise_pubkey, 32);
        pos += 32;
    }

    // Ed25519 public key TLV (optional)
    if(announce->has_ed25519_key) {
        if(pos + 2 + 32 > buf_sz) return 0;
        buf[pos++] = BC_TLV_ED25519_PUBKEY;
        buf[pos++] = 32;
        memcpy(&buf[pos], announce->ed25519_pubkey, 32);
        pos += 32;
    }

    return pos;
}

uint16_t bc_encode_message(
    uint8_t* buf,
    uint16_t buf_sz,
    const BcMessage* msg) {
    uint16_t pos = 0;

    // Flags byte
    if(pos + 1 > buf_sz) return 0;
    uint8_t msg_flags = 0;
    if(msg->has_sender_peer_id) msg_flags |= 0x10; // hasSenderPeerID
    buf[pos++] = msg_flags;

    // Timestamp (8 bytes)
    if(pos + 8 > buf_sz) return 0;
    put_u64_be(&buf[pos], msg->timestamp ? msg->timestamp : bc_timestamp_now());
    pos += 8;

    // ID (1 byte len + data) — use a short random ID
    uint8_t id_str[] = "00000000";
    uint8_t id_len = 8;
    if((size_t)(pos + 1 + id_len) > buf_sz) return 0;
    buf[pos++] = id_len;
    memcpy(&buf[pos], id_str, id_len);
    pos += id_len;

    // Sender nickname (1 byte len + data)
    uint8_t sender_len = strlen(msg->sender);
    if((size_t)(pos + 1 + sender_len) > buf_sz) return 0;
    buf[pos++] = sender_len;
    memcpy(&buf[pos], msg->sender, sender_len);
    pos += sender_len;

    // Content (2 byte len + data)
    uint16_t content_len = strlen(msg->content);
    if((size_t)(pos + 2 + content_len) > buf_sz) return 0;
    put_u16_be(&buf[pos], content_len);
    pos += 2;
    memcpy(&buf[pos], msg->content, content_len);
    pos += content_len;

    // Optional: sender peer ID
    if(msg->has_sender_peer_id) {
        uint8_t pid_hex_len = BC_SENDER_ID_SIZE * 2;
        if((size_t)(pos + 1 + pid_hex_len) > buf_sz) return 0;
        buf[pos++] = pid_hex_len;
        // Encode peer ID as hex string
        for(uint8_t i = 0; i < BC_SENDER_ID_SIZE; i++) {
            uint8_t hi = (msg->sender_peer_id[i] >> 4) & 0xF;
            uint8_t lo = msg->sender_peer_id[i] & 0xF;
            buf[pos++] = hi < 10 ? '0' + hi : 'a' + hi - 10;
            buf[pos++] = lo < 10 ? '0' + lo : 'a' + lo - 10;
        }
    }

    return pos;
}

uint16_t bc_build_announce_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const BcAnnounce* announce) {
    // Encode payload first into temp area
    uint8_t payload[128];
    uint16_t payload_len = bc_encode_announce(payload, sizeof(payload), announce);
    if(payload_len == 0) return 0;

    uint16_t hdr_len = bc_encode_header(
        buf, buf_sz, BC_TYPE_ANNOUNCE, BC_DEFAULT_TTL, 0, sender_id, payload, payload_len);
    if(hdr_len == 0) return 0;
    if(hdr_len + payload_len > buf_sz) return 0;

    memcpy(&buf[hdr_len], payload, payload_len);
    return hdr_len + payload_len;
}

uint16_t bc_build_signed_announce_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const BcAnnounce* announce,
    BcSignFn sign_fn,
    void* sign_ctx) {
    // Encode payload
    uint8_t payload[128];
    uint16_t payload_len = bc_encode_announce(payload, sizeof(payload), announce);
    if(payload_len == 0) return 0;

    // Build packet with HAS_SIGNATURE flag
    uint16_t hdr_len = bc_encode_header(
        buf, buf_sz, BC_TYPE_ANNOUNCE, BC_DEFAULT_TTL,
        BC_FLAG_HAS_SIGNATURE, sender_id, payload, payload_len);
    if(hdr_len == 0) return 0;
    if(hdr_len + payload_len + BC_SIGNATURE_SIZE > buf_sz) return 0;

    memcpy(&buf[hdr_len], payload, payload_len);
    uint16_t data_end = hdr_len + payload_len;

    // Build signing data: same packet but with ttl=0 and no signature
    uint8_t sign_buf[256];
    memcpy(sign_buf, buf, data_end);
    sign_buf[2] = 0; // ttl = 0 for signing

    // Sign and append signature
    uint8_t sig[BC_SIGNATURE_SIZE];
    sign_fn(sign_buf, data_end, sig, sign_ctx);
    memcpy(&buf[data_end], sig, BC_SIGNATURE_SIZE);

    uint16_t total = data_end + BC_SIGNATURE_SIZE;

    // Apply PKCS#7 padding
    total = bc_apply_padding(buf, total, buf_sz);
    return total;
}

uint16_t bc_build_signed_broadcast_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const char* content,
    BcSignFn sign_fn,
    void* sign_ctx) {
    // Android expects raw UTF-8 payload + signature for broadcast messages
    uint16_t content_len = strlen(content);
    if(content_len == 0) return 0;

    uint16_t hdr_len = bc_encode_header(
        buf, buf_sz, BC_TYPE_MESSAGE, BC_DEFAULT_TTL,
        BC_FLAG_HAS_SIGNATURE, sender_id, (const uint8_t*)content, content_len);
    if(hdr_len == 0) return 0;
    if(hdr_len + content_len + BC_SIGNATURE_SIZE > buf_sz) return 0;

    memcpy(&buf[hdr_len], content, content_len);
    uint16_t data_end = hdr_len + content_len;

    // Sign with ttl=0
    uint8_t sign_buf[256];
    if(data_end > sizeof(sign_buf)) return 0;
    memcpy(sign_buf, buf, data_end);
    sign_buf[2] = 0; // ttl = 0 for signing

    uint8_t sig[BC_SIGNATURE_SIZE];
    sign_fn(sign_buf, data_end, sig, sign_ctx);
    memcpy(&buf[data_end], sig, BC_SIGNATURE_SIZE);

    uint16_t total = data_end + BC_SIGNATURE_SIZE;
    total = bc_apply_padding(buf, total, buf_sz);
    return total;
}

uint16_t bc_build_message_packet(
    uint8_t* buf,
    uint16_t buf_sz,
    const uint8_t* sender_id,
    const BcMessage* msg) {
    uint8_t payload[BC_MAX_PAYLOAD];
    uint16_t payload_len = bc_encode_message(payload, sizeof(payload), msg);
    if(payload_len == 0) return 0;

    uint16_t hdr_len = bc_encode_header(
        buf, buf_sz, BC_TYPE_MESSAGE, BC_DEFAULT_TTL, 0, sender_id, payload, payload_len);
    if(hdr_len == 0) return 0;
    if(hdr_len + payload_len > buf_sz) return 0;

    memcpy(&buf[hdr_len], payload, payload_len);
    return hdr_len + payload_len;
}

uint16_t bc_apply_padding(uint8_t* buf, uint16_t data_len, uint16_t buf_sz) {
    // PKCS#7 padding to nearest block of {256, 512, 1024, 2048}
    // Android adds +16 for encryption overhead when selecting block size
    static const uint16_t blocks[] = {256, 512, 1024, 2048};
    uint16_t total_with_overhead = data_len + 16;
    uint16_t target = 0;
    for(int i = 0; i < 4; i++) {
        if(total_with_overhead <= blocks[i]) {
            target = blocks[i];
            break;
        }
    }
    if(target == 0 || target > buf_sz) return data_len; // too large, skip padding

    uint16_t padding_needed = target - data_len;
    // PKCS#7: pad value must be 1-255. If 0 or >255, skip padding.
    if(padding_needed == 0 || padding_needed > 255) return data_len;
    memset(&buf[data_len], (uint8_t)padding_needed, padding_needed);
    return target;
}

// ── Decoding ─────────────────────────────────────────────────────────

bool bc_decode_header(const uint8_t* data, uint16_t len, BcPacketHeader* hdr) {
    if(len < BC_HEADER_SIZE + BC_SENDER_ID_SIZE) return false;

    hdr->version = data[0];
    if(hdr->version != BC_VERSION) return false;

    hdr->type = data[1];
    hdr->ttl = data[2];
    hdr->timestamp = get_u64_be(&data[3]);
    hdr->flags = data[11];
    hdr->payload_len = get_u16_be(&data[12]);
    memcpy(hdr->sender_id, &data[14], BC_SENDER_ID_SIZE);

    hdr->has_recipient = (hdr->flags & BC_FLAG_HAS_RECIPIENT) != 0;
    if(hdr->has_recipient && len >= BC_HEADER_SIZE + BC_SENDER_ID_SIZE * 2) {
        memcpy(hdr->recipient_id, &data[22], BC_SENDER_ID_SIZE);
    }

    return true;
}

bool bc_decode_announce(
    const uint8_t* payload,
    uint16_t payload_len,
    BcAnnounce* announce) {
    memset(announce, 0, sizeof(BcAnnounce));

    uint16_t pos = 0;
    while(pos + 2 <= payload_len) {
        uint8_t tlv_type = payload[pos++];
        uint8_t tlv_len = payload[pos++];
        if(pos + tlv_len > payload_len) break;

        switch(tlv_type) {
        case BC_TLV_NICKNAME: {
            uint8_t copy_len = tlv_len > BC_MAX_NICKNAME ? BC_MAX_NICKNAME : tlv_len;
            memcpy(announce->nickname, &payload[pos], copy_len);
            announce->nickname[copy_len] = '\0';
            break;
        }
        case BC_TLV_NOISE_PUBKEY:
            if(tlv_len == 32) {
                memcpy(announce->noise_pubkey, &payload[pos], 32);
                announce->has_noise_key = true;
            }
            break;
        case BC_TLV_ED25519_PUBKEY:
            if(tlv_len == 32) {
                memcpy(announce->ed25519_pubkey, &payload[pos], 32);
                announce->has_ed25519_key = true;
            }
            break;
        default:
            break;
        }
        pos += tlv_len;
    }
    return announce->nickname[0] != '\0';
}

bool bc_decode_message(
    const uint8_t* payload,
    uint16_t payload_len,
    BcMessage* msg) {
    memset(msg, 0, sizeof(BcMessage));
    uint16_t pos = 0;

    // Flags
    if(pos + 1 > payload_len) return false;
    uint8_t msg_flags = payload[pos++];

    // Timestamp
    if(pos + 8 > payload_len) return false;
    msg->timestamp = get_u64_be(&payload[pos]);
    pos += 8;

    // ID (skip)
    if(pos + 1 > payload_len) return false;
    uint8_t id_len = payload[pos++];
    if(pos + id_len > payload_len) return false;
    pos += id_len;

    // Sender nickname
    if(pos + 1 > payload_len) return false;
    uint8_t sender_len = payload[pos++];
    if(pos + sender_len > payload_len) return false;
    uint8_t copy = sender_len > BC_MAX_NICKNAME ? BC_MAX_NICKNAME : sender_len;
    memcpy(msg->sender, &payload[pos], copy);
    msg->sender[copy] = '\0';
    pos += sender_len;

    // Content
    if(pos + 2 > payload_len) return false;
    uint16_t content_len = get_u16_be(&payload[pos]);
    pos += 2;
    if(pos + content_len > payload_len) return false;
    uint16_t ccopy = content_len > BC_MAX_MSG_CONTENT ? BC_MAX_MSG_CONTENT : content_len;
    memcpy(msg->content, &payload[pos], ccopy);
    msg->content[ccopy] = '\0';
    pos += content_len;

    // Optional fields based on flags
    if(msg_flags & 0x10) { // hasSenderPeerID
        // Skip optional fields before it (isRelay, isPrivate, hasOriginalSender, hasRecipientNickname)
        if(msg_flags & 0x01) { // isRelay — skip originalSender
            if(pos + 1 <= payload_len) {
                uint8_t skip = payload[pos++];
                pos += skip;
            }
        }
        if(msg_flags & 0x08) { // hasRecipientNickname — skip
            if(pos + 1 <= payload_len) {
                uint8_t skip = payload[pos++];
                pos += skip;
            }
        }
        // Now sender peer ID
        if(pos + 1 <= payload_len) {
            uint8_t pid_len = payload[pos++];
            if(pos + pid_len <= payload_len && pid_len >= BC_SENDER_ID_SIZE * 2) {
                // Decode hex to bytes
                for(uint8_t i = 0; i < BC_SENDER_ID_SIZE && i * 2 + 1 < pid_len; i++) {
                    uint8_t hi = payload[pos + i * 2];
                    uint8_t lo = payload[pos + i * 2 + 1];
                    hi = (hi >= 'a') ? hi - 'a' + 10 : (hi >= 'A') ? hi - 'A' + 10 : hi - '0';
                    lo = (lo >= 'a') ? lo - 'a' + 10 : (lo >= 'A') ? lo - 'A' + 10 : lo - '0';
                    msg->sender_peer_id[i] = (hi << 4) | lo;
                }
                msg->has_sender_peer_id = true;
            }
        }
    }

    return msg->sender[0] != '\0' && msg->content[0] != '\0';
}
