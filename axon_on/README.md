# Axon On

Detect and activate Axon body cameras via BLE on the Flipper Zero.

## What it does

Axon body cameras (used by law enforcement) advertise over BLE using service UUID `0xFE6C` and OUI prefix `00:25:DF`. This app:

1. **Scans** for nearby Axon cameras using passive BLE scanning
2. **Alerts** you with vibration and LED flash (red + blue) when a camera is detected
3. **Broadcasts** a BLE advertisement packet that triggers Axon cameras to begin recording

## Usage

1. Open **Axon On** from the Bluetooth app category
2. Select **Scan for Cameras** to begin scanning
3. Detected cameras appear in a list with MAC address and signal strength
4. Press **OK** to start broadcasting the activation command
5. Press **OK** again to stop broadcasting
6. Press **Back** to stop scanning and return to the menu

The "TX" indicator in the top-right corner shows when broadcast is active.

## How it works

- **Scanning** uses the GAP scanning API to passively listen for BLE advertisements. Devices are matched by MAC OUI (`00:25:DF`) or by the presence of service UUID `0xFE6C` in their advertising data.
- **Broadcasting** uses the Extra Beacon API to transmit a captured Axon service data payload alongside the Flipper's normal BLE stack. The previous beacon state is saved and restored on exit.

## Credits

- v1.0 by .leviathan (original BLE Spam fork)
- v2.0 rewrite by @KaraZajac — added scanning, alerts, cleaned up architecture
- Axon protocol research: [AxonCadabra](https://github.com/WithLoveFromMinneapolis/AxonCadabra)
