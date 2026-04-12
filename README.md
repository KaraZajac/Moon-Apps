# Moon-Apps

External apps for [Moon Firmware](https://github.com/KaraZajac/Moon-Firmware).

Forked from [Momentum-Apps](https://github.com/Next-Flip/Momentum-Apps) with modifications for Moon Firmware's XIP flash execution architecture.

---

### What's Different from Momentum-Apps?

Moon Firmware runs all user-facing applications as external FAPs loaded via XIP (execute-in-place) from flash. Apps that use SubGhz or NFC device APIs bundle those libraries directly as `fap_private_libs`, making them fully self-contained — no firmware-exported symbols required.

**Apps modified for Moon:**
- **radio_scanner** — bundled `lib/subghz` + device API table
- **tanks** — bundled `lib/subghz` + device API table
- **subghz_playlist** — bundled `lib/subghz` + device API table
- **rolling_flaws** — bundled `lib/subghz` + device API table
- **esubghz_chat** — bundled `lib/subghz` + device API table

### Adding SubGhz Support to an App

To make an external app self-contained with SubGhz device access:

1. Create a library symlink:
   ```
   mkdir -p your_app/lib
   ln -s ../../../../lib/subghz your_app/lib/subghz
   ```

2. Copy the SubGhz API table from the main app:
   ```
   mkdir -p your_app/api
   cp applications/main/subghz/api/subghz_app_api_table.cpp your_app/api/
   cp applications/main/subghz/api/subghz_app_api_table_i.h your_app/api/
   cp applications/main/subghz/api/subghz_app_api_interface.h your_app/api/
   ```

3. Update `application.fam`:
   ```python
   App(
       ...
       fap_private_libs=[Lib(name="subghz")],
       ...
   )
   ```

---

### BLE Apps (Moon Exclusive)

These apps leverage Moon Firmware's Full BLE stack with dual-role central+peripheral, GATT client, custom services, and extended advertising.

| App | Description |
|---|---|
| **BitChat** | BLE mesh chat client. Decentralized peer-to-peer messaging over Bluetooth with no internet. Custom GATT profile, Ed25519 signed packets, Curve25519 key exchange, dual-role connections. Interoperates with Android/iOS BitChat apps. |
| **Meshtastic** | BLE client for Meshtastic mesh networking radios. Connect to Meshtastic nodes, send/receive messages, view node info. Full GATT client with pairing, MTU exchange, and notification support. |
| **Tracker Detector** | Passive BLE scanner for nearby trackers (Apple AirTag/FindMy, Samsung SmartTag, Tile, Chipolo, Google FMDN). Alerts when a tracker may be following you. Can play sound on detected trackers via GATT. |
| **BLE Lock Tester** | BLE smart lock security tester. Scan and identify locks, run automated default PIN tests against known vulnerable lock profiles, manual GATT characteristic writes. For authorized testing only. |
| **WhisperPair** | CVE-2025-36911 Fast Pair vulnerability scanner. Tests BLE audio devices for the Key-Based Pairing authentication bypass. 4 KBP test strategies, 18-device database, pairing mode detection. |
| **BLE Connect** | General-purpose BLE device browser. Scan, connect, browse GATT services/characteristics, read/write values, save device profiles. |
| **BLE Beacon Toolkit** | Create and broadcast custom iBeacon, Eddystone-URL, and AltBeacon advertisements. |
| **BLE Cloner** | Scan a BLE device, capture its advertising data, and replay it as a clone. |
| **BT Scanner** | Simple BLE device scanner with RSSI display. |
| **BT Explorer** | Connect to BLE devices and browse GATT services. |
| **BLE RSSI Tracker** | Lock onto a BLE device and track its signal strength with a live graph. |

---

### Base

Forked from [Momentum-Apps](https://github.com/Next-Flip/Momentum-Apps). All original Momentum app modifications, asset pack support, and community apps are preserved.
