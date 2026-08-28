# ESP32 iPhone Bluetooth Trigger + Apple/Google BLE Beacon

A PlatformIO/ESP-IDF firmware for a **classic ESP32** that runs three Bluetooth
roles at the same time:

1. a Classic Bluetooth HID mouse used as an iPhone Shortcuts connection trigger;
2. an unofficial Apple OpenHaystack-compatible BLE advertiser;
3. an experimental Google Find Hub BLE advertiser.

The HID profile sends no movement or clicks. Presenting as a mouse keeps the iOS
on-screen keyboard available, while connecting the device can start a Shortcuts
automation. Existing Bluetooth bonds are stored in NVS and the ESP32 actively
reconnects after a restart.

> This is an independent, experimental project. It is not certified by or
> affiliated with Apple, Google or Espressif. Use it only with devices you own
> and comply with local privacy and anti-stalking laws.

## Hardware compatibility

Use an original ESP32 with both **Bluetooth Classic (BR/EDR)** and BLE, such as:

- ESP32-WROOM-32;
- ESP32-WROVER;
- Wemos D1 Mini ESP32;
- common `esp32dev` boards based on the original ESP32.

ESP32-C3, C6, H2 and S3 do not provide Bluetooth Classic and therefore cannot run
the HID-trigger portion of this firmware. They can run separate BLE-only tracker
firmware, but not this three-role project.

## Privacy-safe configuration

Per-device values never need to be committed.

1. Copy the public template:

   **Windows PowerShell**

   ```powershell
   Copy-Item include/tracker_config.example.h include/tracker_config.local.h
   ```

   **Linux/macOS**

   ```bash
   cp include/tracker_config.example.h include/tracker_config.local.h
   ```

2. Edit only `include/tracker_config.local.h`.
3. Build normally.

`tracker_config.local.h` is excluded by `.gitignore`. A clean clone without that
file uses `tracker_config.example.h`, whose Apple and Google identities are empty.
The project therefore remains buildable without exposing a real tracker identity.

Never put Apple private keys, Apple IDs, passwords, Google account secrets,
`secrets.json`, EIKs or authentication tokens in this repository.

## Configuration reference

| Setting | Default | Meaning |
|---|---:|---|
| `TRACKER_DEVICE_NAME` | `ESP32-BT-Trigger` | Name displayed in iPhone Bluetooth settings |
| `TRACKER_DEVICE_PROVIDER` | `ESP32 DIY` | HID provider string |
| `TRACKER_RECONNECT_PERIOD_S` | `8` | Retry period while the paired phone is unavailable |
| `TRACKER_APPLE_ADVERTISEMENT_KEY_BASE64` | empty | Public Apple P-224 advertisement key; empty disables Apple |
| `TRACKER_GOOGLE_ADVERTISEMENT_EID_HEX` | empty | 20-byte Google EID as 40 hex characters; empty disables Google |
| `TRACKER_APPLE_SLOT_MS` | `6000` | Continuous Apple time window when both providers are enabled |
| `TRACKER_GOOGLE_SLOT_MS` | `2000` | Continuous Google time window when both providers are enabled |
| `TRACKER_APPLE_ADV_INTERVAL_MIN` | `0x0640` | Apple minimum interval: 1 second |
| `TRACKER_APPLE_ADV_INTERVAL_MAX` | `0x0C80` | Apple maximum interval: 2 seconds |
| `TRACKER_GOOGLE_ADV_INTERVAL` | `0x00A0` | Google interval: 100 ms |
| `TRACKER_BLE_TX_POWER_DBM` | `9` | Radio power: `-12`, `-9`, `-6`, `-3`, `0`, `3`, `6` or `9` dBm |

BLE interval values use 0.625 ms units. The Apple defaults match the official
OpenHaystack ESP32 firmware. The ESP32 has one legacy BLE advertising set, so the
firmware time-multiplexes Apple and Google. Long continuous windows are used to
avoid repeatedly interrupting the Apple frame.

If only one valid provider identity is configured, that provider remains active
continuously and no scheduler task is created.

## Apple identity

Apple/OpenHaystack uses a P-224 key pair:

- **private key**: used by the retrieval backend to decrypt reports; never place it
  in the firmware or repository;
- **advertisement key**: public 28-byte X coordinate transmitted by the ESP32;
- **hashed key**: `Base64(SHA-256(advertisement key))`, queried by
  macless-haystack/Find My Web.

Generate a new identity locally:

```bash
python -m pip install cryptography
python tools/generate_keys.py
```

Save the private key in your retrieval system and paste only the Base64
advertisement key into:

```cpp
#define TRACKER_APPLE_ADVERTISEMENT_KEY_BASE64 "..."
```

At boot the firmware prints the exact hashed key derived from the advertisement
key. It must match the hash shown by the currently used Find My Web installation:

```text
Apple key loaded; Find My Web hash: ...
```

A valid radio packet cannot produce locations if the backend is querying a
different hash or does not have the matching private key.

The Apple frame follows the OpenHaystack layout: Apple company ID `0x004C`,
offline-finding type `0x12`, a static-random BLE address containing key bytes
0..5, and manufacturer data containing key bytes 6..27 plus the two overwritten
address bits.

## Google identity

Register the device with
[GoogleFindMyTools](https://github.com/leonboe1/GoogleFindMyTools) or a compatible
Google Find Hub tool. Paste only its 20-byte advertisement EID:

```cpp
#define TRACKER_GOOGLE_ADVERTISEMENT_EID_HEX \
    "00112233445566778899aabbccddeeff00112233"
```

The value must contain exactly 40 hexadecimal characters without spaces, commas
or `0x`. It is not an Apple key, Google account secret, private key or EIK.
Authentication material remains on the machine running the Google provider.

## Build and flash

Install [PlatformIO](https://platformio.org/), then run:

```bash
pio run
pio run -t upload
pio device monitor
```

For a clean first installation or after changing the Bluetooth identity:

```bash
pio run -t erase
pio run -t upload
```

The tested build uses:

- PlatformIO `espressif32@6.9.0`;
- ESP-IDF 5.3.1;
- CMake 3.30 override;
- a custom partition table for the dual-mode Bluetooth stack.

ESP-IDF is required because the Classic HID Device role is not available in the
usual precompiled Arduino Bluetooth libraries.

## iPhone setup

1. Flash the firmware and power the ESP32.
2. Open **Settings > Bluetooth** on the iPhone.
3. Pair with the configured `TRACKER_DEVICE_NAME`.
4. In **Shortcuts > Automation**, create a Bluetooth automation for that device.
5. Disable confirmation if your iOS version permits automatic execution.

The firmware reconnects to the first stored Classic Bluetooth bond every
`TRACKER_RECONNECT_PERIOD_S` seconds. If the device identity/profile changes,
forget the old device on the iPhone and erase NVS before pairing again.

## Expected serial output

With both providers enabled:

```text
Starting ESP32-BT-Trigger (Classic HID + Apple + Google BLE, local config)...
Apple key loaded; Find My Web hash: ...
Google Find Hub EID loaded
Apple Find My beacon: advertising
Google Find Hub beacon: advertising
```

The provider messages should follow the configured 6-second/2-second windows.
`PHONE CONNECTED` confirms the Classic HID trigger independently of BLE tracking.

## Troubleshooting

### Apple produces no reports

1. Compare the boot hash with the hash stored by the active backend.
2. Confirm the backend has the private key matching the firmware advertisement
   key.
3. Confirm macless-haystack returns reports for that exact hash.
4. Wait for another Apple device to pass near the beacon; reports are not created
   by the ESP32 itself.
5. Keep the default Apple 1–2 second interval and a multi-second Apple slot.

### Google works but Apple does not

This does not prove that the Apple identity is correct. The two providers use
independent keys, packet formats and backends. Check the Apple boot hash first.

### Neither beacon starts

Both configured identities are empty or malformed. Apple must decode from Base64
to exactly 28 bytes; Google must contain exactly 40 hexadecimal characters.

### iPhone no longer reconnects

Forget the device on iPhone, erase the ESP32 NVS and pair again. Classic bonds
survive ordinary firmware uploads.

### Build exceeds the default partition

Keep `partitions.csv` and the custom partition settings in `sdkconfig.defaults`.
The combined Classic HID and BLE stack is larger than the default 1 MB app slot.

## Project layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | Classic HID, BLE frame construction and provider scheduler |
| `include/tracker_config.example.h` | Public documented configuration template |
| `include/tracker_config.local.h` | Private local configuration, ignored by Git |
| `tools/generate_keys.py` | Local Apple P-224 identity generator |
| `sdkconfig.defaults` | ESP-IDF dual-mode Bluetooth/HID configuration |
| `partitions.csv` | App partition sized for the combined stack |

## Security notes

Advertisement identities are public over radio but still identify a specific
tracker. Do not publish real Apple advertisement keys or Google EIDs. Never
publish private/decryption keys or provider account credentials. Static tracker
identities may trigger unwanted-tracker alerts on nearby phones.

## Credits

- [OpenHaystack / SEEMOO Lab](https://github.com/seemoo-lab/openhaystack) for the
  reverse-engineered Apple offline-finding protocol and ESP32 reference firmware.
- [macless-haystack](https://github.com/dchristl/macless-haystack) for Mac-less
  Apple report retrieval.
- [GoogleFindMyTools](https://github.com/leonboe1/GoogleFindMyTools) for
  experimental Google Find Hub tooling.

## License

MIT. See [LICENSE](LICENSE).
