/*
 * ============================================================
 *   ESP32 BT TRIGGER + APPLE + GOOGLE  -  Classic + BLE
 * ============================================================
 *
 *   Three services on one classic ESP32:
 *
 *   1) CLASSIC Bluetooth HID mouse  -> the "trigger": connecting to your
 *      iPhone fires an iOS Shortcuts automation (no input is sent).
 *      Auto-reconnects to the last paired phone.
 *
 *   2) BLE "Find My" beacon (OpenHaystack)  -> broadcasts a public key like
 *      a lost AirTag. Nearby Apple devices anonymously report its encrypted
 *      location to Apple; you fetch & decrypt them yourself (macless-haystack).
 *
 *   3) BLE Google Find Hub beacon -> broadcasts the 20-byte advertisement
 *      EID registered with GoogleFindMyTools.
 *
 *   IMPORTANT - this is the UNOFFICIAL (reverse-engineered) Find My route.
 *   It is NOT a certified "Works with Find My" accessory. See README.
 *
 *   Apple and Google advertisements are time-multiplexed on the single legacy
 *   BLE advertising set. Classic HID remains active through BTDM.
 * ============================================================
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidd_api.h"
#include "esp_gap_ble_api.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

/*
 * Private values live in tracker_config.local.h, which is ignored by Git.
 * A clean clone falls back to the safe, empty public template.
 */
#if __has_include("tracker_config.local.h")
#include "tracker_config.local.h"
#define TRACKER_CONFIGURATION_SOURCE "local"
#else
#include "tracker_config.example.h"
#define TRACKER_CONFIGURATION_SOURCE "example"
#endif

#define DEVICE_NAME         TRACKER_DEVICE_NAME
#define DEVICE_PROVIDER     TRACKER_DEVICE_PROVIDER
#define RECONNECT_PERIOD_S  TRACKER_RECONNECT_PERIOD_S
#define APPLE_SLOT_MS       TRACKER_APPLE_SLOT_MS
#define GOOGLE_SLOT_MS      TRACKER_GOOGLE_SLOT_MS
#define APPLE_ADV_INT_MIN   TRACKER_APPLE_ADV_INTERVAL_MIN
#define APPLE_ADV_INT_MAX   TRACKER_APPLE_ADV_INTERVAL_MAX
#define GOOGLE_ADV_INTERVAL TRACKER_GOOGLE_ADV_INTERVAL

static const char *TAG = "BTHID";
static const char apple_findmy_advertisement_key_base64[] =
    TRACKER_APPLE_ADVERTISEMENT_KEY_BASE64;
static const char google_findhub_eid_hex[] =
    TRACKER_GOOGLE_ADVERTISEMENT_EID_HEX;

// ---------- Classic HID (mouse) ----------------------------------------

// Standard 3-button mouse report descriptor (never actually sent).
static const uint8_t hid_descriptor[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0
};

static esp_hidd_app_param_t app_param;
static esp_hidd_qos_param_t  qos;
static volatile bool s_connected = false;
static volatile bool s_have_peer = false;
static esp_bd_addr_t s_peer_addr;

static void make_discoverable(void)
{
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

static bool load_paired_peer(esp_bd_addr_t out)
{
    int num = esp_bt_gap_get_bond_device_num();
    if (num <= 0) return false;
    if (num > 4) num = 4;
    esp_bd_addr_t list[4];
    if (esp_bt_gap_get_bond_device_list(&num, list) != ESP_OK || num <= 0) return false;
    memcpy(out, list[0], sizeof(esp_bd_addr_t));
    return true;
}

static void hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
        case ESP_HIDD_INIT_EVT:
            if (param->init.status == ESP_HIDD_SUCCESS)
                esp_bt_hid_device_register_app(&app_param, &qos, &qos);
            else
                ESP_LOGE(TAG, "HID init FAILED");
            break;
        case ESP_HIDD_REGISTER_APP_EVT:
            if (param->register_app.status == ESP_HIDD_SUCCESS) {
                ESP_LOGI(TAG, "HID app registered -> discoverable");
                make_discoverable();
                if (load_paired_peer(s_peer_addr)) {
                    s_have_peer = true;
                    esp_bt_hid_device_connect(s_peer_addr);
                }
            }
            break;
        case ESP_HIDD_OPEN_EVT:
            s_connected = true;
            ESP_LOGI(TAG, ">>> PHONE CONNECTED <<<  (Shortcuts automation can run)");
            break;
        case ESP_HIDD_CLOSE_EVT:
            s_connected = false;
            ESP_LOGI(TAG, "Phone disconnected -> will retry");
            make_discoverable();
            break;
        default:
            break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Pairing OK");
                if (load_paired_peer(s_peer_addr)) s_have_peer = true;
            } else {
                ESP_LOGE(TAG, "Pairing FAILED");
            }
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        default:
            break;
    }
}

// ---------- BLE tracking beacons (Apple OpenHaystack + Google Find Hub) -

// Apple "offline finding" advertisement template (31 bytes). The public key
// is injected into the BLE random address (bytes 0..5) and the payload.
static uint8_t findmy_adv_data[31] = {
    0x1e,             // length of the AD structure (30)
    0xff,             // manufacturer specific data
    0x4c, 0x00,       // company id: Apple (0x004C)
    0x12, 0x19,       // Apple type 0x12 (offline finding), length 0x19 (25)
    0x00,             // status
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // key bytes 6..27 (filled in)
    0x00,             // public_key[0] >> 6
    0x00              // hint
};

// Google Find Hub legacy FHN frame: FEAA service data + 20-byte EID.
static uint8_t google_adv_data[29] = {
    0x02, 0x01, 0x06, // general-discoverable, BR/EDR not supported
    0x19, 0x16,       // 25-byte service-data AD structure
    0xAA, 0xFE,       // Google service UUID 0xFEAA, little endian
    0x41,             // FHN frame with unwanted-tracking protection indication
    // bytes 8..27: 20-byte advertisement EID
    // byte 28: hashed flags (0 = unsupported battery indication)
};

typedef enum {
    BEACON_APPLE,
    BEACON_GOOGLE,
} beacon_provider_t;

static esp_ble_adv_params_t apple_adv_params;
static esp_ble_adv_params_t google_adv_params;
static esp_bd_addr_t apple_random_addr;
static uint8_t findmy_public_key[28];
static bool apple_beacon_enabled = false;
static bool google_beacon_enabled = false;
static volatile bool ble_adv_running = false;
static volatile bool ble_switching = false;
static volatile beacon_provider_t active_beacon = BEACON_APPLE;
static volatile beacon_provider_t pending_beacon = BEACON_APPLE;

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_apple_key(uint8_t out[28])
{
    const size_t input_len = strlen(apple_findmy_advertisement_key_base64);
    if (input_len == 0) return false;

    size_t output_len = 0;
    int result = mbedtls_base64_decode(
        out,
        28,
        &output_len,
        reinterpret_cast<const unsigned char *>(
            apple_findmy_advertisement_key_base64
        ),
        input_len
    );
    if (result != 0 || output_len != 28) {
        ESP_LOGE(TAG, "Apple advertisement key must be valid Base64 decoding to 28 bytes");
        return false;
    }
    return true;
}

static bool decode_google_eid(uint8_t out[20])
{
    if (strlen(google_findhub_eid_hex) == 0) return false;
    if (strlen(google_findhub_eid_hex) != 40) {
        ESP_LOGE(TAG, "Google EID must contain exactly 40 hexadecimal characters");
        return false;
    }
    for (size_t i = 0; i < 20; ++i) {
        int high = hex_nibble(google_findhub_eid_hex[i * 2]);
        int low = hex_nibble(google_findhub_eid_hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            ESP_LOGE(TAG, "Google EID contains a non-hexadecimal character");
            return false;
        }
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void prepare_tracking_beacons(void)
{
    // Maximum-rate, non-connectable Apple advertisement using a random address.
    memset(&apple_adv_params, 0, sizeof(apple_adv_params));
    apple_adv_params.adv_int_min       = APPLE_ADV_INT_MIN;
    apple_adv_params.adv_int_max       = APPLE_ADV_INT_MAX;
    apple_adv_params.adv_type          = ADV_TYPE_NONCONN_IND;
    apple_adv_params.own_addr_type     = BLE_ADDR_TYPE_RANDOM;
    apple_adv_params.channel_map       = ADV_CHNL_ALL;
    apple_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    apple_beacon_enabled = decode_apple_key(findmy_public_key);
    if (apple_beacon_enabled) {
        // BLE random address from the first 6 key bytes; top 2 bits = 11.
        apple_random_addr[0] = findmy_public_key[0] | 0xC0;
        apple_random_addr[1] = findmy_public_key[1];
        apple_random_addr[2] = findmy_public_key[2];
        apple_random_addr[3] = findmy_public_key[3];
        apple_random_addr[4] = findmy_public_key[4];
        apple_random_addr[5] = findmy_public_key[5];

        // Payload: key bytes 6..27, then the top 2 bits of key byte 0.
        memcpy(&findmy_adv_data[7], &findmy_public_key[6], 22);
        findmy_adv_data[29] = findmy_public_key[0] >> 6;
        unsigned char digest[32];
        unsigned char digest_b64[48] = {};
        size_t digest_b64_len = 0;
        mbedtls_sha256(findmy_public_key, sizeof(findmy_public_key), digest, 0);
        if (mbedtls_base64_encode(digest_b64, sizeof(digest_b64),
                                  &digest_b64_len, digest, sizeof(digest)) == 0) {
            ESP_LOGI(TAG, "Apple key loaded; Find My Web hash: %.*s",
                     (int)digest_b64_len, (const char *)digest_b64);
        } else {
            ESP_LOGI(TAG, "Apple Find My advertisement key loaded");
        }
    } else {
        ESP_LOGW(TAG, "Apple Find My beacon disabled: paste its Base64 advertisement key");
    }

    // Google uses the controller public address and the same aggressive interval.
    memset(&google_adv_params, 0, sizeof(google_adv_params));
    google_adv_params.adv_int_min       = GOOGLE_ADV_INTERVAL;
    google_adv_params.adv_int_max       = GOOGLE_ADV_INTERVAL;
    google_adv_params.adv_type          = ADV_TYPE_NONCONN_IND;
    google_adv_params.own_addr_type     = BLE_ADDR_TYPE_PUBLIC;
    google_adv_params.channel_map       = ADV_CHNL_ALL;
    google_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    uint8_t google_eid[20];
    google_beacon_enabled = decode_google_eid(google_eid);
    if (google_beacon_enabled) {
        memcpy(&google_adv_data[8], google_eid, sizeof(google_eid));
        ESP_LOGI(TAG, "Google Find Hub EID loaded");
    } else {
        ESP_LOGW(TAG, "Google Find Hub beacon disabled: paste its 40-character EID");
    }
}

static const char *beacon_name(beacon_provider_t provider)
{
    return provider == BEACON_APPLE ? "Apple Find My" : "Google Find Hub";
}

static void configure_beacon(beacon_provider_t provider)
{
    pending_beacon = provider;
    esp_err_t err;

    if (provider == BEACON_APPLE) {
        err = esp_ble_gap_set_rand_addr(apple_random_addr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Apple random address failed: %s", esp_err_to_name(err));
            ble_switching = false;
            return;
        }
        err = esp_ble_gap_config_adv_data_raw(findmy_adv_data, sizeof(findmy_adv_data));
    } else {
        err = esp_ble_gap_config_adv_data_raw(google_adv_data, sizeof(google_adv_data));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s data configuration failed: %s",
                 beacon_name(provider), esp_err_to_name(err));
        ble_switching = false;
    }
}

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
            esp_ble_adv_params_t *params =
                pending_beacon == BEACON_APPLE ? &apple_adv_params : &google_adv_params;
            esp_err_t err = esp_ble_gap_start_advertising(params);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s start request failed: %s",
                         beacon_name(pending_beacon), esp_err_to_name(err));
                ble_switching = false;
            }
            break;
        }
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                active_beacon = pending_beacon;
                ble_adv_running = true;
                ble_switching = false;
                ESP_LOGI(TAG, "%s beacon: advertising", beacon_name(active_beacon));
            } else {
                ble_adv_running = false;
                ble_switching = false;
                ESP_LOGE(TAG, "%s beacon: advertising FAILED",
                         beacon_name(pending_beacon));
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ble_adv_running = false;
            if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                configure_beacon(pending_beacon);
            } else {
                ble_switching = false;
                ESP_LOGE(TAG, "BLE advertising stop failed");
            }
            break;
        default:
            break;
    }
}

static void beacon_switch_task(void *arg)
{
    (void)arg;
    while (true) {
        const uint32_t slot_ms =
            active_beacon == BEACON_APPLE ? APPLE_SLOT_MS : GOOGLE_SLOT_MS;
        vTaskDelay(pdMS_TO_TICKS(slot_ms));
        if (!apple_beacon_enabled || !google_beacon_enabled ||
            !ble_adv_running || ble_switching) {
            continue;
        }

        pending_beacon =
            active_beacon == BEACON_APPLE ? BEACON_GOOGLE : BEACON_APPLE;
        ble_switching = true;
        esp_err_t err = esp_ble_gap_stop_advertising();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BLE switch to %s failed: %s",
                     beacon_name(pending_beacon), esp_err_to_name(err));
            ble_switching = false;
            continue;
        }

        // The GAP callbacks complete stop -> configure -> start asynchronously.
        // Do not choose the next slot duration from the previous provider.
        while (ble_switching) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static esp_power_level_t configured_tx_power(void)
{
    switch (TRACKER_BLE_TX_POWER_DBM) {
        case -12: return ESP_PWR_LVL_N12;
        case -9:  return ESP_PWR_LVL_N9;
        case -6:  return ESP_PWR_LVL_N6;
        case -3:  return ESP_PWR_LVL_N3;
        case 0:   return ESP_PWR_LVL_N0;
        case 3:   return ESP_PWR_LVL_P3;
        case 6:   return ESP_PWR_LVL_P6;
        case 9:   return ESP_PWR_LVL_P9;
        default:
            ESP_LOGW(TAG, "Unsupported TX power %d dBm; using 0 dBm",
                     TRACKER_BLE_TX_POWER_DBM);
            return ESP_PWR_LVL_N0;
    }
}

// ---------- main -------------------------------------------------------

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting %s (Classic HID + Apple + Google BLE, %s config)...",
             DEVICE_NAME, TRACKER_CONFIGURATION_SOURCE);

    // Classic HID app (mouse)
    app_param.name         = DEVICE_NAME;
    app_param.description   = "BT Pointer";
    app_param.provider      = DEVICE_PROVIDER;
    app_param.subclass      = ESP_HID_CLASS_MIC;
    app_param.desc_list     = (uint8_t *)hid_descriptor;
    app_param.desc_list_len = sizeof(hid_descriptor);
    memset(&qos, 0, sizeof(esp_hidd_qos_param_t));

    // DUAL MODE: keep BLE memory (do NOT release it) and enable BTDM
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    const esp_power_level_t tx_power = configured_tx_power();
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, tx_power));
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, tx_power));
    ESP_ERROR_CHECK(esp_bredr_tx_power_set(tx_power, tx_power));
    ESP_LOGI(TAG, "Bluetooth TX power configured at %d dBm (BLE + Classic)",
             TRACKER_BLE_TX_POWER_DBM);

    // Classic side
    esp_bt_gap_register_callback(gap_cb);
    esp_bt_hid_device_register_callback(hidd_cb);
    esp_bt_hid_device_init();
    esp_bt_gap_set_device_name(DEVICE_NAME);

    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // BLE Apple + Google tracking side
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_cb));
    prepare_tracking_beacons();
    if (apple_beacon_enabled) {
        configure_beacon(BEACON_APPLE);
    } else if (google_beacon_enabled) {
        configure_beacon(BEACON_GOOGLE);
    } else {
        ESP_LOGW(TAG, "No tracking key configured; Classic HID remains active");
    }
    if (apple_beacon_enabled && google_beacon_enabled) {
        xTaskCreate(beacon_switch_task, "beacon_switch", 3072, nullptr, 5, nullptr);
    }

    ESP_LOGI(TAG, "Ready. Classic '%s', Apple beacon %s, Google beacon %s.",
             DEVICE_NAME,
             apple_beacon_enabled ? "active" : "disabled",
             google_beacon_enabled ? "active" : "disabled");

    int countdown = 0;
    while (true) {
        if (s_connected) {
            countdown = 0;
        } else if (s_have_peer) {
            if (countdown <= 0) {
                ESP_LOGI(TAG, "Reconnecting: calling the last paired phone...");
                esp_bt_hid_device_connect(s_peer_addr);
                countdown = RECONNECT_PERIOD_S;
            }
            countdown--;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
