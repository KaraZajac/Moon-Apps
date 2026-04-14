#include "bitchat_protocol.h"
#include <string.h>
#include <furi.h>
#include <moon/settings.h>

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
    // Flipper RTC stores local time, so we apply the UTC offset from settings
    extern uint32_t furi_hal_rtc_get_timestamp(void);
    uint32_t local_ts = furi_hal_rtc_get_timestamp();
    // utc_offset_hours: e.g. -4 for EDT means local is UTC-4, so UTC = local + 4 hours
    int32_t offset_seconds = -moon_settings.utc_offset_hours * 3600;
    return ((uint64_t)local_ts + offset_seconds) * 1000ULL;
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

    // Build signing data: copy the packet, set ttl=0 and clear HAS_SIGNATURE flag.
    // CRITICAL: must use the SAME timestamp as the actual packet (not a new one).
    // Android's toBinaryDataForSigning() copies the received packet and modifies
    // ttl + flags in place, then re-encodes + pads.
    uint8_t sign_buf[BC_PAD_BLOCK_256];
    memcpy(sign_buf, buf, data_end);
    sign_buf[2] = 0;  // ttl = 0
    sign_buf[11] &= ~BC_FLAG_HAS_SIGNATURE; // clear only HAS_SIGNATURE bit
    uint16_t sign_data_len = data_end;

    // Apply PKCS#7 padding — Android signs over the PADDED data
    sign_data_len = bc_apply_padding(sign_buf, sign_data_len, sizeof(sign_buf));

    FURI_LOG_I("BcProto", "Signing %d bytes (ttl=0, no HAS_SIG, padded):", sign_data_len);
    FURI_LOG_I("BcProto", "  flags=0x%02X pad_to=%d", sign_buf[11], sign_data_len);

    // Sign the padded data
    uint8_t sig[BC_SIGNATURE_SIZE];
    sign_fn(sign_buf, sign_data_len, sig, sign_ctx);

    // Append signature to the ORIGINAL packet (which has HAS_SIGNATURE set, real ttl)
    memcpy(&buf[data_end], sig, BC_SIGNATURE_SIZE);

    uint16_t total = data_end + BC_SIGNATURE_SIZE;
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

    // Build signing data: copy packet, set ttl=0, clear HAS_SIGNATURE
    // CRITICAL: reuse same timestamp from actual packet header
    uint8_t sign_buf[BC_PAD_BLOCK_256];
    memcpy(sign_buf, buf, data_end);
    sign_buf[2] = 0;  // ttl = 0
    sign_buf[11] &= ~BC_FLAG_HAS_SIGNATURE; // clear only HAS_SIGNATURE bit
    uint16_t sign_data_len = data_end;
    sign_data_len = bc_apply_padding(sign_buf, sign_data_len, sizeof(sign_buf));

    uint8_t sig[BC_SIGNATURE_SIZE];
    sign_fn(sign_buf, sign_data_len, sig, sign_ctx);
    memcpy(&buf[data_end], sig, BC_SIGNATURE_SIZE);

    uint16_t total = data_end + BC_SIGNATURE_SIZE;
    // Skip padding — BLE write limit ~247 bytes, Android handles unpadded
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

// ── Fragment Reassembly ─────────────────────────────────────────────

void bc_fragment_init(BcFragmentTable* ft) {
    memset(ft, 0, sizeof(BcFragmentTable));
}

uint16_t bc_fragment_process(
    BcFragmentTable* ft,
    const uint8_t* pkt_data,
    uint16_t pkt_len,
    uint8_t* out_buf,
    uint16_t out_buf_sz) {
    // Parse outer header
    BcPacketHeader hdr;
    if(!bc_decode_header(pkt_data, pkt_len, &hdr)) return 0;
    if(hdr.type != BC_TYPE_FRAGMENT) return 0;

    uint16_t payload_offset = BC_HEADER_SIZE + BC_SENDER_ID_SIZE;
    if(payload_offset + BC_FRAG_HEADER_SIZE >= pkt_len) return 0;

    const uint8_t* frag_hdr = &pkt_data[payload_offset];
    // Parse fragment sub-header: fragmentID(8) + index(2) + total(2) + origType(1)
    const uint8_t* frag_id = &frag_hdr[0];
    uint16_t index = (frag_hdr[8] << 8) | frag_hdr[9];
    uint16_t total = (frag_hdr[10] << 8) | frag_hdr[11];
    uint8_t orig_type = frag_hdr[12];

    if(total == 0 || total > BC_FRAG_MAX_PARTS || index >= total) return 0;

    const uint8_t* frag_data = &pkt_data[payload_offset + BC_FRAG_HEADER_SIZE];
    uint16_t frag_data_len = pkt_len - payload_offset - BC_FRAG_HEADER_SIZE;

    uint32_t now = furi_get_tick();

    // Find existing fragment set or allocate new one
    BcFragmentSet* set = NULL;
    for(int i = 0; i < BC_MAX_FRAG_SETS; i++) {
        if(ft->sets[i].active && memcmp(ft->sets[i].fragment_id, frag_id, 8) == 0) {
            set = &ft->sets[i];
            break;
        }
    }

    if(!set) {
        // Expire old sets and find a free slot
        for(int i = 0; i < BC_MAX_FRAG_SETS; i++) {
            if(ft->sets[i].active &&
               (now - ft->sets[i].start_tick) > BC_FRAG_TIMEOUT_MS) {
                ft->sets[i].active = false;
            }
            if(!ft->sets[i].active && !set) {
                set = &ft->sets[i];
            }
        }
        if(!set) return 0; // no free slot

        memset(set, 0, sizeof(BcFragmentSet));
        memcpy(set->fragment_id, frag_id, 8);
        set->total_parts = total;
        set->original_type = orig_type;
        set->start_tick = now;
        set->active = true;
        // Save header from first fragment for reassembly
        memcpy(set->header, pkt_data, BC_HEADER_SIZE + BC_SENDER_ID_SIZE);
    }

    // Store fragment data
    uint16_t offset = index * BC_FRAG_MAX_DATA;
    if(offset + frag_data_len <= sizeof(set->data)) {
        memcpy(&set->data[offset], frag_data, frag_data_len);
        uint16_t end = offset + frag_data_len;
        if(end > set->data_len) set->data_len = end;
        set->received_mask |= (1u << index);
    }

    // Check if all parts received
    uint16_t expected_mask = (1u << total) - 1;
    if((set->received_mask & expected_mask) != expected_mask) return 0;

    // Reassemble: build a complete packet with the original type
    if(BC_HEADER_SIZE + BC_SENDER_ID_SIZE + set->data_len > out_buf_sz) {
        set->active = false;
        return 0;
    }

    memcpy(out_buf, set->header, BC_HEADER_SIZE + BC_SENDER_ID_SIZE);
    out_buf[1] = set->original_type; // restore original packet type
    put_u16_be(&out_buf[12], set->data_len); // update payload length
    out_buf[2] = 0; // reassembled packets get TTL=0 (no further relay)
    memcpy(&out_buf[BC_HEADER_SIZE + BC_SENDER_ID_SIZE], set->data, set->data_len);

    uint16_t total_len = BC_HEADER_SIZE + BC_SENDER_ID_SIZE + set->data_len;
    set->active = false;

    FURI_LOG_I("BcFrag", "Reassembled %d bytes (type=0x%02X, %d parts)",
        total_len, set->original_type, total);

    return total_len;
}

bool bc_fragment_send(
    const uint8_t* pkt_data,
    uint16_t pkt_len,
    const uint8_t* sender_id,
    uint8_t ttl,
    BcFragSendFn send_fn,
    void* send_ctx) {

    uint16_t payload_offset = BC_HEADER_SIZE + BC_SENDER_ID_SIZE;
    if(pkt_len <= payload_offset) return false;

    uint8_t orig_type = pkt_data[1];
    const uint8_t* payload = &pkt_data[payload_offset];
    uint16_t payload_len = pkt_len - payload_offset;

    uint16_t total = (payload_len + BC_FRAG_MAX_DATA - 1) / BC_FRAG_MAX_DATA;
    if(total > BC_FRAG_MAX_PARTS) return false;

    // Generate random fragment ID
    uint8_t frag_id[8];
    furi_hal_random_fill_buf(frag_id, 8);

    for(uint16_t i = 0; i < total; i++) {
        uint16_t offset = i * BC_FRAG_MAX_DATA;
        uint16_t chunk_len = payload_len - offset;
        if(chunk_len > BC_FRAG_MAX_DATA) chunk_len = BC_FRAG_MAX_DATA;

        uint8_t frag_pkt[512];
        // Build fragment packet header
        uint16_t frag_payload_len = BC_FRAG_HEADER_SIZE + chunk_len;
        uint16_t hdr_len = bc_encode_header(
            frag_pkt, sizeof(frag_pkt), BC_TYPE_FRAGMENT, ttl, 0,
            sender_id, NULL, frag_payload_len);

        // Fragment sub-header
        uint16_t pos = hdr_len;
        memcpy(&frag_pkt[pos], frag_id, 8); pos += 8;
        frag_pkt[pos++] = (i >> 8) & 0xFF;
        frag_pkt[pos++] = i & 0xFF;
        frag_pkt[pos++] = (total >> 8) & 0xFF;
        frag_pkt[pos++] = total & 0xFF;
        frag_pkt[pos++] = orig_type;

        // Fragment data
        memcpy(&frag_pkt[pos], &payload[offset], chunk_len);
        pos += chunk_len;

        if(!send_fn(frag_pkt, pos, send_ctx)) return false;
    }
    return true;
}

// ── Deduplication ───────────────────────────────────────────────────

// Simple FNV-1a hash for payload dedup
static uint32_t bc_hash_payload(const uint8_t* data, uint16_t len) {
    uint32_t hash = 0x811C9DC5u;
    for(uint16_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x01000193u;
    }
    return hash;
}

void bc_dedup_init(BcDedupTable* dt) {
    memset(dt, 0, sizeof(BcDedupTable));
}

bool bc_dedup_check(BcDedupTable* dt, const BcPacketHeader* hdr,
                    const uint8_t* payload, uint16_t payload_len) {
    uint32_t phash = bc_hash_payload(payload, payload_len);
    uint64_t now_ms = hdr->timestamp; // use packet timestamp for consistency

    // Check existing entries
    for(uint16_t i = 0; i < BC_DEDUP_MAX_ENTRIES; i++) {
        if(!dt->entries[i].active) continue;

        // Expire old entries
        if(now_ms > dt->entries[i].timestamp &&
           (now_ms - dt->entries[i].timestamp) > BC_DEDUP_WINDOW_MS) {
            dt->entries[i].active = false;
            continue;
        }

        // Check for match
        if(dt->entries[i].timestamp == hdr->timestamp &&
           dt->entries[i].payload_hash == phash &&
           memcmp(dt->entries[i].sender_id, hdr->sender_id, BC_SENDER_ID_SIZE) == 0) {
            return true; // DUPLICATE
        }
    }

    // Not a duplicate — add to table
    BcDedupEntry* entry = &dt->entries[dt->next_idx];
    entry->timestamp = hdr->timestamp;
    memcpy(entry->sender_id, hdr->sender_id, BC_SENDER_ID_SIZE);
    entry->payload_hash = phash;
    entry->active = true;
    dt->next_idx = (dt->next_idx + 1) % BC_DEDUP_MAX_ENTRIES;
    if(dt->count < BC_DEDUP_MAX_ENTRIES) dt->count++;

    return false; // NOT duplicate
}

// ── Relay ───────────────────────────────────────────────────────────

uint8_t bc_relay_should_forward(
    const BcPacketHeader* hdr,
    const uint8_t* our_sender_id,
    uint8_t peer_count) {
    // Don't relay our own packets
    if(memcmp(hdr->sender_id, our_sender_id, BC_SENDER_ID_SIZE) == 0) return 0;

    // Don't relay if TTL is 0
    if(hdr->ttl == 0) return 0;

    uint8_t new_ttl = hdr->ttl - 1;

    // Always relay if TTL >= BC_RELAY_ALWAYS_TTL
    if(hdr->ttl >= BC_RELAY_ALWAYS_TTL) return new_ttl;

    // Probability-based relay dampening based on network size
    uint8_t probability;
    if(peer_count <= 10) {
        probability = 100;
    } else if(peer_count <= 30) {
        probability = 85;
    } else if(peer_count <= 50) {
        probability = 70;
    } else if(peer_count <= 100) {
        probability = 55;
    } else {
        probability = 40;
    }

    // Simple random check using furi_hal_random
    uint8_t roll;
    furi_hal_random_fill_buf(&roll, 1);
    roll = roll % 100;

    return (roll < probability) ? new_ttl : 0;
}
