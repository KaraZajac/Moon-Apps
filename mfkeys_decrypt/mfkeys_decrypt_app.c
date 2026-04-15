#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include <subghz/subghz_keystore.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <toolbox/stream/stream.h>

#define TAG "MfkeysDecryptApp"

#define KEYSTORE_DIR  APP_DATA_PATH("keystore")
#define OUTPUT_FILE   APP_DATA_PATH("decrypted_keys.txt")
#define MAX_NAME_LEN  256

// Max RAW data we'll attempt to decrypt in one shot (bytes)
#define RAW_DECRYPT_MAX 480

typedef enum {
    KeystoreFileStandard,
    KeystoreFileRaw,
    KeystoreFileUnknown,
} KeystoreFileType;

/**
 * Peek at a file's header to determine if it's a standard keystore or RAW keystore.
 * Also returns the size (in bytes) of the encrypted RAW payload when applicable.
 */
static KeystoreFileType
    detect_keystore_type(Storage* storage, const char* path, size_t* raw_data_size) {
    KeystoreFileType type = KeystoreFileUnknown;
    uint32_t version = 0;

    FuriString* filetype = furi_string_alloc();
    FuriString* str_temp = furi_string_alloc();

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_existing(ff, path)) break;
        if(!flipper_format_read_header(ff, filetype, &version)) break;

        const char* ft = furi_string_get_cstr(filetype);

        if(strcmp(ft, "Flipper SubGhz Keystore File") == 0 && version == 0) {
            type = KeystoreFileStandard;
        } else if(strcmp(ft, "Flipper SubGhz Keystore RAW File") == 0 && version == 0) {
            type = KeystoreFileRaw;
            // Figure out how many bytes of decrypted RAW data are in the file.
            // We need to advance past the Encrypt_data field, then measure
            // the remaining hex stream (2 hex chars = 1 byte).
            uint32_t encryption = 0;
            if(!flipper_format_read_uint32(ff, "Encryption", &encryption, 1)) break;

            // Skip past IV (read but discard)
            uint8_t iv_discard[16];
            flipper_format_read_hex(ff, "IV", iv_discard, 16);

            // Read the Encrypt_data field value (e.g. "RAW")
            if(!flipper_format_read_string(ff, "Encrypt_data", str_temp)) break;

            // After reading that field the stream cursor sits right before the
            // hex blob.  Account for the trailing newline the reader consumed.
            Stream* stream = flipper_format_get_raw_stream(ff);
            // skip the newline between field value and hex data
            uint8_t skip;
            stream_read(stream, &skip, 1);

            size_t remaining = stream_size(stream) - stream_tell(stream);
            // remaining is hex chars; each pair = 1 byte
            if(raw_data_size) *raw_data_size = remaining / 2;
        }
    } while(0);

    flipper_format_free(ff);
    furi_string_free(str_temp);
    furi_string_free(filetype);
    return type;
}

/**
 * Decrypt a standard keystore file and write KEY:TYPE:NAME lines to out_file.
 * Returns the number of keys written.
 */
static size_t decrypt_standard_keystore(const char* path, File* out_file, FuriString* line) {
    SubGhzKeystore* keystore = subghz_keystore_alloc();
    if(!keystore) {
        FURI_LOG_E(TAG, "Failed to allocate keystore");
        return 0;
    }

    if(!subghz_keystore_load(keystore, path)) {
        FURI_LOG_W(TAG, "Failed to load keystore %s", path);
        furi_string_printf(line, "# (failed to decrypt)\n");
        storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));
        subghz_keystore_free(keystore);
        return 0;
    }

    SubGhzKeyArray_t* keys = subghz_keystore_get_data(keystore);
    size_t key_count = SubGhzKeyArray_size(*keys);
    FURI_LOG_I(TAG, "  %zu key entries", key_count);

    for(size_t i = 0; i < key_count; i++) {
        SubGhzKey* k = SubGhzKeyArray_get(*keys, i);
        furi_string_printf(
            line,
            "%016llX:%u:%s\n",
            (unsigned long long)k->key,
            (unsigned)k->type,
            furi_string_get_cstr(k->name));
        storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));
    }

    subghz_keystore_free(keystore);
    return key_count;
}

/**
 * Decrypt a RAW keystore file and write a hex dump to out_file.
 * Returns true on success.
 */
static bool
    decrypt_raw_keystore(const char* path, size_t data_size, File* out_file, FuriString* line) {
    if(data_size == 0 || data_size > RAW_DECRYPT_MAX) {
        FURI_LOG_E(TAG, "RAW data size %zu out of range", data_size);
        furi_string_printf(line, "# (RAW data too large: %zu bytes)\n", data_size);
        storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));
        return false;
    }

    uint8_t* buffer = malloc(data_size);
    if(!buffer) {
        FURI_LOG_E(TAG, "Failed to allocate %zu bytes for RAW decrypt", data_size);
        return false;
    }

    bool ok = subghz_keystore_raw_get_data(path, 0, buffer, data_size);
    if(!ok) {
        FURI_LOG_W(TAG, "Failed to decrypt RAW keystore %s", path);
        furi_string_printf(line, "# (failed to decrypt RAW data)\n");
        storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));
        free(buffer);
        return false;
    }

    FURI_LOG_I(TAG, "  RAW decrypted %zu bytes", data_size);

    // Write as hex dump, 32 bytes per line
    for(size_t off = 0; off < data_size; off += 32) {
        size_t chunk = data_size - off;
        if(chunk > 32) chunk = 32;

        furi_string_printf(line, "%04X: ", (unsigned)off);
        for(size_t j = 0; j < chunk; j++) {
            furi_string_cat_printf(line, "%02X", buffer[off + j]);
            if(j < chunk - 1) furi_string_cat_printf(line, " ");
        }
        furi_string_cat_printf(line, "\n");
        storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));
    }

    free(buffer);
    return true;
}

int mfkeys_decrypt_app(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    size_t total_keys = 0;
    size_t total_files = 0;

    // Open output file for writing
    File* out_file = storage_file_alloc(storage);
    if(!storage_file_open(out_file, OUTPUT_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(TAG, "Failed to open output file %s", OUTPUT_FILE);
        storage_file_free(out_file);
        furi_record_close(RECORD_STORAGE);
        return -1;
    }

    // Write header
    const char* header = "# Decrypted SubGhz Keystore\n"
                         "# Standard files: KEY:TYPE:NAME\n"
                         "# RAW files: hex dump of decrypted data\n"
                         "#\n";
    storage_file_write(out_file, header, strlen(header));

    // Open keystore directory and iterate files
    File* dir = storage_file_alloc(storage);
    if(!storage_dir_open(dir, KEYSTORE_DIR)) {
        FURI_LOG_E(TAG, "Failed to open directory %s", KEYSTORE_DIR);
        storage_dir_close(dir);
        storage_file_free(dir);
        storage_file_free(out_file);
        furi_record_close(RECORD_STORAGE);
        return -1;
    }

    char name[MAX_NAME_LEN];
    FileInfo fileinfo;
    FuriString* file_path = furi_string_alloc();
    FuriString* line = furi_string_alloc();

    while(storage_dir_read(dir, &fileinfo, name, MAX_NAME_LEN)) {
        if(file_info_is_dir(&fileinfo)) continue;

        furi_string_printf(file_path, "%s/%s", KEYSTORE_DIR, name);
        const char* path = furi_string_get_cstr(file_path);

        FURI_LOG_I(TAG, "Processing: %s", path);

        // Detect file type
        size_t raw_size = 0;
        KeystoreFileType ftype = detect_keystore_type(storage, path, &raw_size);

        if(ftype == KeystoreFileStandard) {
            furi_string_printf(line, "\n# File: %s (standard keystore)\n", name);
            storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));

            size_t count = decrypt_standard_keystore(path, out_file, line);
            total_keys += count;

        } else if(ftype == KeystoreFileRaw) {
            furi_string_printf(
                line, "\n# File: %s (RAW keystore, %zu bytes)\n", name, raw_size);
            storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));

            decrypt_raw_keystore(path, raw_size, out_file, line);

        } else {
            FURI_LOG_W(TAG, "Unknown file type: %s, skipping", name);
            furi_string_printf(line, "\n# File: %s (unknown format, skipped)\n", name);
            storage_file_write(out_file, furi_string_get_cstr(line), furi_string_size(line));
        }

        total_files++;
    }

    FURI_LOG_I(
        TAG,
        "Done — %zu files processed, %zu keys written to %s",
        total_files,
        total_keys,
        OUTPUT_FILE);

    furi_string_free(line);
    furi_string_free(file_path);

    storage_dir_close(dir);
    storage_file_free(dir);
    storage_file_free(out_file);
    furi_record_close(RECORD_STORAGE);

    return 0;
}
