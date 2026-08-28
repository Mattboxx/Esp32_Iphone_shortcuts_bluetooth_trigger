#pragma once

/*
 * Public configuration template.
 *
 * For a private build, copy this file to:
 *   include/tracker_config.local.h
 *
 * tracker_config.local.h is ignored by Git. Never commit tracker identities.
 */

// Classic Bluetooth identity shown by iPhone during pairing.
#define TRACKER_DEVICE_NAME             "ESP32-BT-Trigger"
#define TRACKER_DEVICE_PROVIDER         "ESP32 DIY"
#define TRACKER_RECONNECT_PERIOD_S      8

// Apple OpenHaystack advertisement key: Base64, exactly 28 decoded bytes.
// This is the public advertisement identity, NOT the private P-224 key.
// Empty string disables Apple advertising.
#define TRACKER_APPLE_ADVERTISEMENT_KEY_BASE64 ""

// Google Find Hub advertisement EID: exactly 40 hexadecimal characters.
// Do not paste account secrets, EIKs, commas, spaces or a 0x prefix.
// Empty string disables Google advertising.
#define TRACKER_GOOGLE_ADVERTISEMENT_EID_HEX   ""

// Provider time windows. The classic ESP32 has one legacy advertising set,
// therefore Apple and Google are time-multiplexed when both are enabled.
#define TRACKER_APPLE_SLOT_MS            6000
#define TRACKER_GOOGLE_SLOT_MS           2000

// BLE interval units are 0.625 ms. OpenHaystack uses 0x0640..0x0C80
// (1..2 seconds). Google uses 0x00A0 (100 ms) during its shorter slot.
#define TRACKER_APPLE_ADV_INTERVAL_MIN   0x0640
#define TRACKER_APPLE_ADV_INTERVAL_MAX   0x0C80
#define TRACKER_GOOGLE_ADV_INTERVAL      0x00A0

// Supported values: -12, -9, -6, -3, 0, 3, 6 or 9 dBm.
#define TRACKER_BLE_TX_POWER_DBM          9
