/*
 * Classic Bluetooth (BR/EDR) HID mouse - original ESP32 only.
 *
 * The iPhone sees a Classic BT mouse. No report is ever sent: the act of
 * CONNECTING is the trigger for the Shortcuts automation. With Classic HID
 * the DEVICE has to call the phone back after a reboot or a dropped link,
 * so we actively reconnect, cycling through every paired phone and starting
 * from the last one that was connected.
 */
#include "sdkconfig.h"
#if CONFIG_BT_CLASSIC_ENABLED

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidd_api.h"

#include "transport.h"

static const char *TAG = "BT-CLASSIC";

#define MAX_BONDS 8

/* Standard MOUSE report descriptor (3 buttons + X/Y + wheel). We never send
 * reports: being a pointing device keeps the iOS on-screen keyboard usable
 * (an HID keyboard would hide it). */
static const uint8_t hid_descriptor[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

static esp_hidd_app_param_t s_app;
static esp_hidd_qos_param_t s_qos;
static char                 s_name[BTT_NAME_MAX + 1];
static uint16_t             s_reconnect_s = 8;

static volatile bool s_registered = false;
static volatile bool s_enabled    = true;
static volatile bool s_pairable   = true;
static volatile bool s_connected  = false;
static volatile bool s_connecting = false;

static esp_bd_addr_t s_last;          /* last connected phone (from NVS) */
static bool          s_have_last = false;
static int           s_rr_index  = 0; /* round-robin over the bond list */
static int           s_countdown = 0;

/* ---------- persistence of the last connected phone ---------- */

static void last_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_last);
    if (nvs_open("btt", NVS_READONLY, &h) == ESP_OK) {
        s_have_last = nvs_get_blob(h, "last", s_last, &len) == ESP_OK && len == sizeof(s_last);
        nvs_close(h);
    }
}

static void last_save(const esp_bd_addr_t a)
{
    if (s_have_last && memcmp(a, s_last, sizeof(s_last)) == 0) {
        return;
    }
    memcpy(s_last, a, sizeof(s_last));
    s_have_last = true;
    nvs_handle_t h;
    if (nvs_open("btt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "last", s_last, sizeof(s_last));
        nvs_commit(h);
        nvs_close(h);
    }
}

static int bond_list(esp_bd_addr_t *list)
{
    int num = esp_bt_gap_get_bond_device_num();
    if (num <= 0) {
        return 0;
    }
    if (num > MAX_BONDS) {
        num = MAX_BONDS;
    }
    if (esp_bt_gap_get_bond_device_list(&num, list) != ESP_OK) {
        return 0;
    }
    return num;
}

/* Pick who to call next: the last connected phone first, then the others. */
static bool next_peer(esp_bd_addr_t out)
{
    esp_bd_addr_t list[MAX_BONDS];
    int n = bond_list(list);
    if (n == 0) {
        return false;
    }
    if (s_rr_index == 0 && s_have_last) {
        for (int i = 0; i < n; i++) {
            if (memcmp(list[i], s_last, sizeof(esp_bd_addr_t)) == 0) {
                memcpy(out, s_last, sizeof(esp_bd_addr_t));
                s_rr_index = 1;
                return true;
            }
        }
    }
    memcpy(out, list[s_rr_index % n], sizeof(esp_bd_addr_t));
    s_rr_index = (s_rr_index + 1) % (n + 1);
    return true;
}

/* ---------- visibility ---------- */

static void apply_scan_mode(void)
{
    if (!s_registered) {
        return;
    }
    if (!s_enabled || s_connected) {
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    } else {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                 s_pairable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    }
}

static void try_connect(void)
{
    esp_bd_addr_t peer;
    if (!s_registered || !s_enabled || s_connected || !next_peer(peer)) {
        return;
    }
    ESP_LOGI(TAG, "Calling paired phone %02x:%02x:%02x:%02x:%02x:%02x",
             peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
    s_connecting = true;
    esp_bt_hid_device_connect(peer);
}

/* ---------- callbacks ---------- */

static void hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
        case ESP_HIDD_INIT_EVT:
            if (param->init.status == ESP_HIDD_SUCCESS) {
                esp_bt_hid_device_register_app(&s_app, &s_qos, &s_qos);
            } else {
                ESP_LOGE(TAG, "HID init FAILED");
            }
            break;

        case ESP_HIDD_REGISTER_APP_EVT:
            if (param->register_app.status == ESP_HIDD_SUCCESS) {
                ESP_LOGI(TAG, "HID mouse registered");
                s_registered = true;
                apply_scan_mode();
                s_countdown = 0;   /* call the paired phone right away */
                try_connect();
                s_countdown = s_reconnect_s;
            } else {
                ESP_LOGE(TAG, "HID register FAILED");
            }
            break;

        case ESP_HIDD_OPEN_EVT:
            if (param->open.status != ESP_HIDD_SUCCESS) {
                s_connecting = false;
                break;
            }
            if (param->open.conn_status == ESP_HIDD_CONN_STATE_CONNECTED) {
                s_connected  = true;
                s_connecting = false;
                s_rr_index   = 0;
                last_save(param->open.bd_addr);
                ESP_LOGI(TAG, ">>> PHONE CONNECTED <<<  (the Shortcuts automation runs now)");
                apply_scan_mode();
                if (!s_enabled) {           /* switched off while connecting */
                    esp_bt_hid_device_disconnect();
                }
            }
            break;

        case ESP_HIDD_CLOSE_EVT:
            if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTED) {
                bool was = s_connected;
                s_connected  = false;
                s_connecting = false;
                if (was) {
                    ESP_LOGI(TAG, "Phone disconnected");
                }
                apply_scan_mode();
                s_countdown = was ? 2 : s_reconnect_s;   /* quick retry after a drop */
            }
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
                ESP_LOGI(TAG, "Paired with '%s'", (char *)param->auth_cmpl.device_name);
            } else {
                ESP_LOGW(TAG, "Pairing failed (%d)", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT: {
            /* Just Works: accept, unless pairing is locked and this is a new phone. */
            bool known = false;
            esp_bd_addr_t list[MAX_BONDS];
            int n = bond_list(list);
            for (int i = 0; i < n; i++) {
                if (memcmp(list[i], param->cfm_req.bda, sizeof(esp_bd_addr_t)) == 0) {
                    known = true;
                }
            }
            bool accept = s_pairable || known;
            if (!accept) {
                ESP_LOGW(TAG, "Pairing refused: pairing window closed (power-cycle to pair a new phone)");
            }
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, accept);
            break;
        }

        default:
            break;
    }
}

/* ---------- public API ---------- */

void transport_init(const btt_config_t *cfg)
{
    strncpy(s_name, cfg->name, sizeof(s_name) - 1);
    s_reconnect_s = cfg->reconnect_s;
    last_load();

    s_app.name          = s_name;
    s_app.description   = "BT Trigger";
    s_app.provider      = "ESP32 BT Trigger";
    s_app.subclass      = ESP_HID_CLASS_MIC;            /* pointing device */
    s_app.desc_list     = (uint8_t *)hid_descriptor;
    s_app.desc_list_len = sizeof(hid_descriptor);
    memset(&s_qos, 0, sizeof(s_qos));

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_bt_gap_register_callback(gap_cb);
    esp_bt_gap_set_device_name(s_name);

    /* Class of Device: peripheral / pointing device (mouse icon on iOS). */
    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = 0x20;                                   /* pointing device */
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);

    /* Just Works pairing (no PIN). */
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));

    esp_bt_hid_device_register_callback(hidd_cb);
    esp_bt_hid_device_init();
}

void transport_set_enabled(bool enabled)
{
    if (enabled == s_enabled) {
        return;
    }
    s_enabled = enabled;
    ESP_LOGI(TAG, "Link %s", enabled ? "ENABLED" : "disabled");
    if (!enabled && (s_connected || s_connecting)) {
        esp_bt_hid_device_disconnect();
    }
    apply_scan_mode();
    if (enabled) {
        s_rr_index  = 0;
        s_countdown = 0;
        try_connect();
        s_countdown = s_reconnect_s;
    }
}

void transport_set_pairable(bool pairable)
{
    if (pairable == s_pairable) {
        return;
    }
    s_pairable = pairable;
    ESP_LOGI(TAG, "Pairing %s", pairable ? "open" : "closed (paired phones still reconnect)");
    apply_scan_mode();
}

void transport_tick_1s(void)
{
    if (!s_enabled || s_connected) {
        return;
    }
    if (--s_countdown <= 0) {
        try_connect();
        s_countdown = s_reconnect_s;
    }
}

link_state_t transport_state(void)
{
    if (!s_enabled) {
        return LINK_IDLE;
    }
    if (s_connected) {
        return LINK_CONNECTED;
    }
    if (esp_bt_gap_get_bond_device_num() <= 0) {
        return LINK_PAIRING;
    }
    return LINK_SEARCHING;
}

bool transport_has_bonds(void)
{
    return esp_bt_gap_get_bond_device_num() > 0;
}

void transport_forget_all(void)
{
    if (s_connected) {
        esp_bt_hid_device_virtual_cable_unplug();
    }
    esp_bd_addr_t list[MAX_BONDS];
    int n = bond_list(list);
    for (int i = 0; i < n; i++) {
        esp_bt_gap_remove_bond_device(list[i]);
    }
    nvs_handle_t h;
    if (nvs_open("btt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "last");
        nvs_commit(h);
        nvs_close(h);
    }
    s_have_last = false;
    ESP_LOGW(TAG, "Forgot %d paired phone(s)", n);
}

const char *transport_name(void)
{
    return "Classic Bluetooth HID";
}

#endif /* CONFIG_BT_CLASSIC_ENABLED */
