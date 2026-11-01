// Supported chip and board presets. Only the original ESP32 works: it has
// Classic Bluetooth, and iOS Shortcuts ignores Bluetooth LE (GATT) accessories.
// A preset only pre-fills the pins (LED, button): the firmware is the same
// for every ESP32 board. Pins come from public board docs:
// double-check against your board if something does not blink.
import { LED } from "./config.js";

const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
const steps = (a, b, s) => Array.from({ length: Math.floor((b - a) / s) + 1 }, (_, i) => a + i * s);

// iPhone test status per chip, shown in the installer and downloads table.
const TESTED = (short, long) => ({ ok: true, short, long });

export const CHIPS = {
  ESP32: {
    env: "esp32",
    transport: "classic",
    label: "ESP32 (original)",
    status: TESTED("v1 tested", "approach tested on iPhone (v1)"),
    txLevels: steps(-12, 9, 3),               // dBm the chip supports
    // 6-11: SPI flash. 16/17: flash/PSRAM on PICO-D4 & WROVER modules. 20,24,28-31: don't exist.
    inputs: range(0, 39).filter((g) => ![6, 7, 8, 9, 10, 11, 16, 17, 20, 24, 28, 29, 30, 31].includes(g)),
    inputOnly: [34, 35, 36, 37, 38, 39],
    noPull: [34, 35, 36, 37, 38, 39],
  },
};

// Chips detected by the flasher that cannot work, with the reason.
export const UNSUPPORTED = {
  "ESP32-S3": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-C3": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-C6": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-C5": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-C61": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-C2": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-H2": "Only Bluetooth LE: iOS Shortcuts does not list BLE accessories as automation triggers. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32), which has Classic Bluetooth.",
  "ESP32-S2": "The ESP32-S2 has no Bluetooth at all. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32).",
  "ESP32-P4": "The ESP32-P4 has no Bluetooth. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32).",
  ESP8266: "The ESP8266 has no Bluetooth. You need an original ESP32 (e.g. ESP32 DevKit / WROOM-32).",
};

const b = (id, chip, name, led, button, note = "") => ({ id, chip, name, led, button, note });
const plain = (gpio, activeLow = false) => ({ type: LED.plain, gpio, activeLow });
const rgb = (gpio) => ({ type: LED.ws2812, gpio, activeLow: false });
const noLed = { type: LED.none, gpio: -1, activeLow: false };

export const BOARDS = [
  b("esp32-devkit", "ESP32", "ESP32 DevKit / DOIT DevKit V1 / NodeMCU-32S", plain(2), 0, "Blue LED on GPIO2, BOOT button on GPIO0. DevKitC v4 has no user LED: harmless."),
  b("d1-mini32", "ESP32", "Wemos / LOLIN D1 mini32", plain(2), 0),
  b("lolin32-lite", "ESP32", "LOLIN32 Lite", plain(22, true), 0),
  b("esp32-cam", "ESP32", "AI-Thinker ESP32-CAM", plain(33, true), -1, "Red LED on GPIO33. No user button (GPIO0 is the boot jumper)."),
  b("m5-atom", "ESP32", "M5Stack Atom Lite / Matrix / Echo", rgb(27), 39),
  b("m5stickc", "ESP32", "M5StickC / StickC Plus", noLed, 37, "Its red LED sits on GPIO10, which is blocked as a flash pin on standard ESP32 modules."),
  b("heltec-v2", "ESP32", "Heltec WiFi Kit 32 / LoRa 32 (V2)", plain(25), 0),
  b("ttgo-tdisplay", "ESP32", "LilyGO TTGO T-Display", noLed, 0),
  b("esp32-generic", "ESP32", "Other ESP32 board", noLed, 0),
];

export const boardsForChip = (chip) => BOARDS.filter((x) => x.chip === chip);
export const outputPins = (chip) => CHIPS[chip].inputs.filter((g) => !CHIPS[chip].inputOnly.includes(g));
