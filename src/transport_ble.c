/*
 * Bluetooth Low Energy HID mouse - ESP32-S3 / ESP32-C3 / ESP32-C6 (and any
 * chip without Classic Bluetooth).
 *
 * The iPhone sees a BLE mouse. No report is ever sent: the act of CONNECTING
 * is the trigger for the Shortcuts automation. With BLE the PHONE reconnects
 * on its own to a bonded HID device as soon as it advertises, so all we have
 * to do is advertise whenever the link should be up.
 */
#include "sdkconfig.h"
#if !CONFIG_BT_CLASSIC_ENABLED && CONFIG_BT_BLE_ENABLED

#include <string.h>

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "esp_hid_common.h"

#include "transport.h"

static const char *TAG = "BT-BLE";

static const uint8_t mouse_report_map[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

static esp_hid_raw_report_map_t s_maps[] = {
    {.data = mouse_report_map, .len = sizeof(mouse_report_map)},
};

static char s_name[BTT_NAME_MAX + 1];

static esp_hid_device_config_t s_hid_cfg = {
    .vendor_id         = 0x16C0,
    .product_id        = 0x05DF,
    .version           = 0x0200,
    .device_name       = s_name,
    .manufacturer_name = "ESP32 BT Trigger",
    .serial_number     = "0001",
    .report_maps       = s_maps,
    .report_maps_len   = 1,
};

static esp_hidd_dev_t *s_dev;

static volatile bool s_started     = false;
static volatile bool s_enabled     = true;
static volatile bool s_pairable    = true;
static volatile bool s_connected   = false;
static volatile bool s_advertising = false;
static volatile bool s_adv_pending = false;   /* start/stop request in flight */
static esp_bd_addr_t s_peer;

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,     /* 20 ms: fast reconnect */
    .adv_int_max       = 0x30,     /* 30 ms */
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* ---------- advertising ---------- */

static void set_adv_data(void)
{
    /* flags | appearance: mouse | 16-bit service UUID: HID (0x1812) */
    uint8_t adv[] = {
        0x02, 0x01, (uint8_t)(s_pairable ? 0x06 : 0x04),   /* 0x06 = discoverable, 0x04 = hidden */
        0x03, 0x19, 0xC2, 0x03,
        0x03, 0x03, 0x12, 0x18,
    };
    esp_ble_gap_config_adv_data_raw(adv, sizeof(adv));

    uint8_t rsp[31];
    size_t n = strlen(s_name);
    if (n > 29) {
        n = 29;
    }
    rsp[0] = (uint8_t)(n + 1);
    rsp[1] = 0x09;                                          /* complete local name */
    memcpy(rsp + 2, s_name, n);
    esp_ble_gap_config_scan_rsp_data_raw(rsp, n + 2);
}

static void update_advertising(void)
{
    if (!s_started || s_adv_pending) {
        return;
    }
    bool want = s_enabled && !s_connected;
    if (want && !s_advertising) {
        s_adv_pending = esp_ble_gap_start_advertising(&s_adv_params) == ESP_OK;
    } else if (!want && s_advertising) {
        s_adv_pending = esp_ble_gap_stop_advertising() == ESP_OK;
    }
}

static bool is_bonded(const esp_bd_addr_t bda)
{
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) {
        return false;
    }
    esp_ble_bond_dev_t list[8];
    if (n > 8) {
        n = 8;
    }
    if (esp_ble_get_bond_device_list(&n, list) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (memcmp(list[i].bd_addr, bda, sizeof(esp_bd_addr_t)) == 0) {
            return true;
        }
    }
    return false;
}

/* ---------- callbacks ---------- */

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            s_adv_pending = false;
            s_advertising = (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
            update_advertising();   /* reconcile if the wanted state changed meanwhile */
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            s_adv_pending = false;
            s_advertising = false;
            update_advertising();
            break;

        case ESP_GAP_BLE_SEC_REQ_EVT: {
            bool accept = s_pairable || is_bonded(param->ble_security.ble_req.bd_addr);
            if (!accept) {
                ESP_LOGW(TAG, "Pairing refused: pairing window closed (power-cycle to pair a new phone)");
            }
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, accept);
            break;
        }

        case ESP_GAP_BLE_NC_REQ_EVT:
            esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, s_pairable);
            break;

        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                ESP_LOGI(TAG, "Paired / encrypted link OK");
            } else {
                ESP_LOGW(TAG, "Pairing failed (0x%x)", param->ble_security.auth_cmpl.fail_reason);
            }
            break;

        default:
            break;
    }
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_CONNECT_EVT) {
        memcpy(s_peer, param->connect.remote_bda, sizeof(s_peer));
        s_advertising = false;   /* the controller stops advertising on connect */
    }
    esp_hidd_gatts_event_handler(event, gatts_if, param);
}

static void hidd_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_hidd_event_t)id) {
        case ESP_HIDD_START_EVENT:
            ESP_LOGI(TAG, "HID mouse service started");
            s_started = true;
            update_advertising();
            break;

        case ESP_HIDD_CONNECT_EVENT:
            s_connected   = true;
            s_advertising = false;
            ESP_LOGI(TAG, ">>> PHONE CONNECTED <<<  (the Shortcuts automation runs now)");
            if (!s_enabled) {
                esp_ble_gap_disconnect(s_peer);
            }
            break;

        case ESP_HIDD_DISCONNECT_EVENT:
            s_connected   = false;
            s_advertising = false;
            ESP_LOGI(TAG, "Phone disconnected");
            update_advertising();
            break;

        default:
            break;
    }
}

/* ---------- public API ---------- */

void transport_init(const btt_config_t *cfg)
{
    strncpy(s_name, cfg->name, sizeof(s_name) - 1);

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    /* TX power for advertising and for the connection (default type). */
    if (cfg->tx_power_set) {
#if CONFIG_IDF_TARGET_ESP32C6
        static const int8_t lv[] = {-15, -12, -9, -6, -3, 0, 3, 6, 9, 12, 15, 18, 20};
        const int base = ESP_PWR_LVL_N15;
#else   /* ESP32-S3 / ESP32-C3 */
        static const int8_t lv[] = {-24, -21, -18, -15, -12, -9, -6, -3, 0, 3, 6, 9, 12, 15, 18, 21};
        const int base = ESP_PWR_LVL_N24;
#endif
        int i = btt_nearest_level(lv, sizeof(lv), cfg->tx_power_dbm);
        esp_power_level_t p = (esp_power_level_t)(base + i);
        if (esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, p) == ESP_OK &&
            esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, p) == ESP_OK) {
            ESP_LOGI(TAG, "TX power set to %+d dBm", lv[i]);
        } else {
            ESP_LOGW(TAG, "Could not set TX power");
        }
    }

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));

    /* Just Works bonding with LE Secure Connections (no PIN). */
    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t   iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));

    esp_ble_gap_set_device_name(s_name);
    set_adv_data();

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_cb));
    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_cfg, ESP_HID_TRANSPORT_BLE, hidd_cb, &s_dev));
}

void transport_set_enabled(bool enabled)
{
    if (enabled == s_enabled) {
        return;
    }
    s_enabled = enabled;
    ESP_LOGI(TAG, "Link %s", enabled ? "ENABLED" : "disabled");
    if (!enabled && s_connected) {
        esp_ble_gap_disconnect(s_peer);
    }
    update_advertising();
}

void transport_set_pairable(bool pairable)
{
    if (pairable == s_pairable) {
        return;
    }
    s_pairable = pairable;
    ESP_LOGI(TAG, "Pairing %s", pairable ? "open" : "closed (paired phones still reconnect)");
    set_adv_data();
}

void transport_tick_1s(void)
{
    /* Safety net: retry advertising if a request was lost. */
    static int stuck = 0;
    if (s_adv_pending && ++stuck > 5) {
        s_adv_pending = false;
    }
    if (!s_adv_pending) {
        stuck = 0;
    }
    update_advertising();
}

link_state_t transport_state(void)
{
    if (!s_enabled) {
        return LINK_IDLE;
    }
    if (s_connected) {
        return LINK_CONNECTED;
    }
    return esp_ble_get_bond_device_num() > 0 ? LINK_SEARCHING : LINK_PAIRING;
}

bool transport_has_bonds(void)
{
    return esp_ble_get_bond_device_num() > 0;
}

void transport_forget_all(void)
{
    if (s_connected) {
        esp_ble_gap_disconnect(s_peer);
    }
    int n = esp_ble_get_bond_device_num();
    if (n > 8) {
        n = 8;
    }
    esp_ble_bond_dev_t list[8];
    if (n > 0 && esp_ble_get_bond_device_list(&n, list) == ESP_OK) {
        for (int i = 0; i < n; i++) {
            esp_ble_remove_bond_device(list[i].bd_addr);
        }
    }
    ESP_LOGW(TAG, "Forgot %d paired phone(s)", n > 0 ? n : 0);
}

const char *transport_name(void)
{
    return "Bluetooth Low Energy HID";
}

#endif /* BLE-only chips */
