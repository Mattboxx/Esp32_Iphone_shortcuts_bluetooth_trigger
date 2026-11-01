// Settings <-> binary blob for the "btcfg" partition.
// The layout is a contract with src/config.c in the firmware. Keep them in sync.

export const CONFIG_OFFSET = 0x180000;
export const SECTOR_SIZE = 4096;
export const CFG_SIZE = 64;
export const NAME_MAX_BYTES = 29;
const MAGIC = [0x42, 0x54, 0x54, 0x47]; // "BTTG"
const VERSION = 1;

export const TRIGGER = { always: 0, switch: 1, pulse: 2 };
export const LED = { none: 0, plain: 1, ws2812: 2 };
export const PULL = { auto: 0, up: 1, down: 2, none: 3 };

export const DEFAULTS = Object.freeze({
  name: "ESP32-BT-Trigger",
  reconnectS: 8,
  pulseS: 10,
  pairWindowS: 0,
  ledGpio: -1,
  ledType: LED.none,
  ledActiveLow: false,
  ledBrightness: 32,
  buttonGpio: -1,
  triggerMode: TRIGGER.always,
  triggerGpio: -1,
  triggerActiveHigh: false,
  triggerPull: PULL.auto,
  debounceMs: 50,
  holdS: 0,
  txPowerDbm: null,      // null = chip default
});

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** UTF-8 encode, truncated to maxBytes without splitting a character. */
export function encodeName(name, maxBytes = NAME_MAX_BYTES) {
  const enc = new TextEncoder();
  let out = new Uint8Array(0);
  for (const ch of String(name)) {
    const next = enc.encode(ch);
    if (out.length + next.length > maxBytes) break;
    const merged = new Uint8Array(out.length + next.length);
    merged.set(out);
    merged.set(next, out.length);
    out = merged;
  }
  return out;
}

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, Math.round(Number(v) || 0)));

/** Settings object -> 64-byte config record. */
export function encodeConfig(s) {
  const b = new Uint8Array(CFG_SIZE);
  const v = new DataView(b.buffer);
  b.set(MAGIC, 0);
  b[4] = VERSION;
  b[5] = CFG_SIZE;
  b.set(encodeName(s.name || DEFAULTS.name), 8);
  v.setUint16(40, clamp(s.reconnectS, 3, 600), true);
  v.setUint16(42, clamp(s.pulseS, 1, 3600), true);
  v.setUint16(44, clamp(s.pairWindowS, 0, 3600), true);
  v.setInt8(46, clamp(s.ledGpio, -1, 63));
  b[47] = clamp(s.ledType, 0, 2);
  b[48] = s.ledActiveLow ? 1 : 0;
  b[49] = clamp(s.ledBrightness, 1, 255);
  v.setInt8(50, clamp(s.buttonGpio, -1, 63));
  b[51] = clamp(s.triggerMode, 0, 2);
  v.setInt8(52, clamp(s.triggerGpio, -1, 63));
  b[53] = s.triggerActiveHigh ? 1 : 0;
  b[54] = clamp(s.triggerPull ?? PULL.auto, 0, 3);
  v.setUint16(56, clamp(s.debounceMs ?? DEFAULTS.debounceMs, 10, 5000), true);
  v.setUint16(58, clamp(s.holdS ?? 0, 0, 3600), true);
  // 0 = default TX power, else dBm + 100 (firmware picks the nearest chip level)
  b[55] = s.txPowerDbm === null || s.txPowerDbm === undefined ? 0 : clamp(s.txPowerDbm, -24, 21) + 100;
  v.setUint32(60, crc32(b.subarray(0, CFG_SIZE - 4)), true);
  return b;
}

/** Full 4 KB sector to write at CONFIG_OFFSET (record + 0xFF padding). */
export function buildConfigSector(s) {
  const sector = new Uint8Array(SECTOR_SIZE).fill(0xff);
  sector.set(encodeConfig(s), 0);
  return sector;
}

/** Bytes read from the board -> settings object, or null if blank/invalid. */
export function decodeConfig(bytes) {
  if (!bytes || bytes.length < CFG_SIZE) return null;
  const b = bytes.subarray(0, CFG_SIZE);
  for (let i = 0; i < 4; i++) if (b[i] !== MAGIC[i]) return null;
  if (b[4] !== VERSION || b[5] !== CFG_SIZE) return null;
  const v = new DataView(b.buffer, b.byteOffset, CFG_SIZE);
  if (crc32(b.subarray(0, CFG_SIZE - 4)) !== v.getUint32(60, true)) return null;
  let end = 8;
  while (end < 8 + NAME_MAX_BYTES && b[end] !== 0) end++;
  return {
    name: new TextDecoder().decode(b.subarray(8, end)) || DEFAULTS.name,
    reconnectS: v.getUint16(40, true),
    pulseS: v.getUint16(42, true),
    pairWindowS: v.getUint16(44, true),
    ledGpio: v.getInt8(46),
    ledType: b[47],
    ledActiveLow: b[48] !== 0,
    ledBrightness: b[49],
    buttonGpio: v.getInt8(50),
    triggerMode: b[51],
    triggerGpio: v.getInt8(52),
    triggerActiveHigh: b[53] !== 0,
    triggerPull: b[54] <= 3 ? b[54] : PULL.auto,
    debounceMs: v.getUint16(56, true) || DEFAULTS.debounceMs,
    holdS: v.getUint16(58, true),
    txPowerDbm: b[55] === 0 ? null : b[55] - 100,
  };
}
