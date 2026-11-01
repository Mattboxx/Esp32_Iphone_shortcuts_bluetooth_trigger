// Chip families and board presets.
// A preset only pre-fills the pins (LED, button): the firmware is the same
// for every board of a chip family. Pins come from public board docs:
// double-check against your board if something does not blink.
import { LED } from "./config.js";

const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);

export const CHIPS = {
  ESP32: {
    env: "esp32",
    transport: "classic",
    label: "ESP32 (original)",
    // 6-11: SPI flash. 16/17: flash/PSRAM on PICO-D4 & WROVER modules. 20,24,28-31: don't exist.
    inputs: range(0, 39).filter((g) => ![6, 7, 8, 9, 10, 11, 16, 17, 20, 24, 28, 29, 30, 31].includes(g)),
    inputOnly: [34, 35, 36, 37, 38, 39],
    noPull: [34, 35, 36, 37, 38, 39],
  },
  "ESP32-S3": {
    env: "esp32s3",
    transport: "ble",
    label: "ESP32-S3",
    inputs: [...range(0, 21), ...range(33, 48)],
    inputOnly: [],
    noPull: [],
  },
  "ESP32-C3": {
    env: "esp32c3",
    transport: "ble",
    label: "ESP32-C3",
    inputs: [...range(0, 10), ...range(18, 21)],
    inputOnly: [],
    noPull: [],
  },
  "ESP32-C6": {
    env: "esp32c6",
    transport: "ble",
    label: "ESP32-C6",
    inputs: range(0, 23),
    inputOnly: [],
    noPull: [],
  },
};

// Chips detected by the flasher that cannot work, with the reason.
export const UNSUPPORTED = {
  "ESP32-S2": "The ESP32-S2 has no Bluetooth radio at all.",
  "ESP32-P4": "The ESP32-P4 has no Bluetooth radio.",
  ESP8266: "The ESP8266 has no Bluetooth radio.",
  "ESP32-H2": "ESP32-H2 support is planned (not built yet).",
  "ESP32-C2": "ESP32-C2 support is planned (not built yet).",
  "ESP32-C5": "ESP32-C5 support is planned (not built yet).",
  "ESP32-C61": "ESP32-C61 support is planned (not built yet).",
};

const b = (id, chip, name, led, button, note = "") => ({ id, chip, name, led, button, note });
const plain = (gpio, activeLow = false) => ({ type: LED.plain, gpio, activeLow });
const rgb = (gpio) => ({ type: LED.ws2812, gpio, activeLow: false });
const noLed = { type: LED.none, gpio: -1, activeLow: false };

export const BOARDS = [
  // ---- original ESP32: Classic Bluetooth ----
  b("esp32-devkit", "ESP32", "ESP32 DevKit / DOIT DevKit V1 / NodeMCU-32S", plain(2), 0, "Blue LED on GPIO2, BOOT button on GPIO0. DevKitC v4 has no user LED: harmless."),
  b("d1-mini32", "ESP32", "Wemos / LOLIN D1 mini32", plain(2), 0),
  b("lolin32-lite", "ESP32", "LOLIN32 Lite", plain(22, true), 0),
  b("esp32-cam", "ESP32", "AI-Thinker ESP32-CAM", plain(33, true), -1, "Red LED on GPIO33. No user button (GPIO0 is the boot jumper)."),
  b("m5-atom", "ESP32", "M5Stack Atom Lite / Matrix / Echo", rgb(27), 39),
  b("m5stickc", "ESP32", "M5StickC / StickC Plus", noLed, 37, "Its red LED sits on GPIO10, which is blocked as a flash pin on standard ESP32 modules."),
  b("heltec-v2", "ESP32", "Heltec WiFi Kit 32 / LoRa 32 (V2)", plain(25), 0),
  b("ttgo-tdisplay", "ESP32", "LilyGO TTGO T-Display", noLed, 0),
  b("esp32-generic", "ESP32", "Other ESP32 board", noLed, 0),

  // ---- ESP32-C3: BLE ----
  b("c3-supermini", "ESP32-C3", "ESP32-C3 SuperMini", plain(8, true), 9),
  b("c3-xiao", "ESP32-C3", "Seeed XIAO ESP32-C3", noLed, 9, "No user LED on this board."),
  b("c3-devkitm", "ESP32-C3", "Espressif ESP32-C3-DevKitM-1 / DevKitC-02", rgb(8), 9),
  b("c3-lolin-mini", "ESP32-C3", "LOLIN C3 Mini", rgb(7), 9),
  b("c3-m5stamp", "ESP32-C3", "M5Stamp C3", rgb(2), 3),
  b("c3-generic", "ESP32-C3", "Other ESP32-C3 board", noLed, 9),

  // ---- ESP32-S3: BLE ----
  b("s3-devkitc", "ESP32-S3", "Espressif ESP32-S3-DevKitC-1", rgb(48), 0, "RGB LED is GPIO48 on v1.0 boards, GPIO38 on v1.1."),
  b("s3-xiao", "ESP32-S3", "Seeed XIAO ESP32-S3", plain(21, true), 0),
  b("s3-atoms3-lite", "ESP32-S3", "M5Stack AtomS3 Lite", rgb(35), 41),
  b("s3-supermini", "ESP32-S3", "ESP32-S3 SuperMini", rgb(48), 0),
  b("s3-generic", "ESP32-S3", "Other ESP32-S3 board", noLed, 0),

  // ---- ESP32-C6: BLE ----
  b("c6-devkitc", "ESP32-C6", "Espressif ESP32-C6-DevKitC-1 / DevKitM-1", rgb(8), 9),
  b("c6-xiao", "ESP32-C6", "Seeed XIAO ESP32-C6", plain(15, true), 9),
  b("c6-generic", "ESP32-C6", "Other ESP32-C6 board", noLed, 9),
];

export const boardsForChip = (chip) => BOARDS.filter((x) => x.chip === chip);
export const outputPins = (chip) => CHIPS[chip].inputs.filter((g) => !CHIPS[chip].inputOnly.includes(g));
