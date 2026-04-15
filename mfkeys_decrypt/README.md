# MFKeys Decrypt

Decrypt encrypted SubGhz keystore files on the Flipper Zero.

## What it does

Reads encrypted keystore files from the app's data directory, decrypts them using the Flipper's built-in SubGhz keystore API, and writes the results to a plaintext file.

## Usage

1. Place encrypted keystore files in `apps_data/mfkeys_decrypt/keystore/` on your SD card
2. Run **MFKeys Decrypt** from the Utilities category
3. Results are written to `apps_data/mfkeys_decrypt/decrypted_keys.txt`

## Output format

**Standard keystores** — one entry per line:
```
KEY:TYPE:NAME
```

**RAW keystores** — hex dump of decrypted data (max 480 bytes):
```
0000: AA BB CC DD ...
```

## Troubleshooting

- Ensure keystore files are placed in the correct directory
- The app requires firmware with SubGhz keystore support
- RAW files larger than 480 bytes will be skipped

## Credits

- Original by .leviathan
- Maintained by @KaraZajac
