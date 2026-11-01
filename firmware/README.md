# Ready-to-flash firmware

The easiest way is the **[web installer](https://mattboxx.github.io/esp32-bt-trigger/)**:
it picks the right file for your chip and writes your settings (name, trigger mode,
LED, button…) at the same time.

| File | Chip | Bluetooth |
|---|---|---|
| `bt-trigger-esp32.factory.bin`   | ESP32 (original)  | Classic BT HID, v1 tested on iPhone |
| `bt-trigger-esp32c3.factory.bin` | ESP32-C3          | BLE HID, beta |
| `bt-trigger-esp32s3.factory.bin` | ESP32-S3          | BLE HID, tested on iPhone (S3-Zero) |
| `bt-trigger-esp32c6.factory.bin` | ESP32-C6          | BLE HID, beta |

Each file is a **merged image** (bootloader + partition table + app) to write at
offset **`0x0`**. It works on any board with **2 MB of flash or more**.
`manifest.json` lists version, sizes and SHA-256 checksums.

## Flash with esptool

```bash
pip install esptool
```

```bash
esptool.py --port <PORT> write_flash 0x0 bt-trigger-esp32.factory.bin
```

`<PORT>` is e.g. `COM5` (Windows), `/dev/ttyUSB0` (Linux) or `/dev/cu.usbserial-XXXX` (macOS).
Writing the merged image also **wipes the saved pairings**: on the iPhone, tap the
device → *Forget This Device* and pair again.

## Default settings

Without the web installer the firmware uses its defaults: name **`ESP32-BT-Trigger`**,
connects whenever powered, "forget phones" button on BOOT (GPIO0 on ESP32/S3, GPIO9
on C3/C6), status LED on GPIO2 (ESP32 only). You can change them later from the web
installer with **Save settings only**, without reflashing: that's also how you switch
to a pin trigger (switch or button) with your own pin, level, resistor and debounce.

## Rebuild

```bash
python scripts/make_release.py
```
