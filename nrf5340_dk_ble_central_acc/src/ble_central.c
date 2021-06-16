#include <zephyr.h>
#include <stdio.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/byteorder.h>

#include "ble_central.h"
#include "message.h"

#define ACC_SERVICE_UUID 0xd4, 0x86, 0x48, 0x24, 0x54, 0xB3, 0x43, 0xA1, \
                     0xBC, 0x20, 0x97, 0x8F, 0xC3, 0x76, 0xC2, 0x75

#define TX_CHAR_UUID 0xED, 0xAA, 0x20, 0x11, 0x92, 0xE7, 0x43, 0x5A, \
                 0xAA, 0xE9, 0x94, 0x43, 0x35, 0x6A, 0xD4, 0xD3

#define BT_UUID_ACC_SERVICE    BT_UUID_DECLARE_128(ACC_SERVICE_UUID)
#define BT_UUID_ACC_SERVICE_TX BT_UUID_DECLARE_128(TX_CHAR_UUID)

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(ble_central);

extern struct k_mbox data_mailbox;

extern struct k_mbox notification_mailbox;

static struct bt_conn *default_conn;
static struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
static struct bt_gatt_exchange_params exchange_params;

static void start_scan();


static void send_notification(enum noti_message msg)
{
    // send code using .info
    struct k_mbox_msg noti_msg = {
        .size = 0,
        .info = msg,
        .tx_data = NULL,
        .tx_block.data = NULL,
        .tx_target_thread = K_ANY
    };
    k_mbox_async_put(&notification_mailbox, &noti_msg, NULL);
}

static uint8_t notify_func(struct bt_conn *conn,
               struct bt_gatt_subscribe_params *params,
               const void *payload, uint16_t length)
{
    if (!payload) {
        params->value_handle = 0U;

        enum noti_message msg = BLE_PERIPHERAL_UNSUBSCRIBED;
        send_notification(msg);

        LOG_INF("[UNSUBSCRIBED]\n");

        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[NOTIFICATION] payload %p length %u\n", payload, length);
    
    uint8_t *data = (uint8_t*) payload;

    if (length == 2) {
        uint16_t code = data[0] | (data[1] << 8);
        LOG_ERR("Error: %d\n", code);
    } else { 
        struct k_mbox_msg data_msg = {
            .size = length,
            .tx_data = data,
            .tx_block.data = NULL,
            .tx_target_thread = K_ANY
        };

        k_mbox_async_put(&data_mailbox, &data_msg, NULL);
    }

    return BT_GATT_ITER_CONTINUE;
}

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
                          struct bt_gatt_exchange_params *params)
{
    LOG_INF("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");
}



static uint8_t discover_func(struct bt_conn *conn,
                 const struct bt_gatt_attr *attr,
                 struct bt_gatt_discover_params *params)
{
    int err;

    if (!attr) {
        printk("Discover complete\n");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[ATTRIBUTE] handle %u\n", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_ACC_SERVICE)) {
        memcpy(&uuid, BT_UUID_ACC_SERVICE_TX, sizeof(uuid));
        discover_params.uuid = &uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed (err %d)\n", err);
        }
    } else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_ACC_SERVICE_TX)) {
        memcpy(&uuid, BT_UUID_GATT_CCC, sizeof(uuid));
        discover_params.uuid = &uuid.uuid;
        discover_params.start_handle = attr->handle + 2;
        discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed (err %d)\n", err);
        }
    } else {
        subscribe_params.notify = notify_func;
        subscribe_params.value = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            LOG_ERR("Subscribe failed (err %d)\n", err);
        } else {
            LOG_DBG("[SUBSCRIBED] %u\n",  attr->handle);
        }

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
    bt_addr_le_t *addr = user_data;
    LOG_DBG("[AD]: %u data_len %u\n", data->type, data->data_len);

    if ( data->type == BT_DATA_NAME_COMPLETE) {
        struct bt_le_conn_param *param;
        uint8_t name[data->data_len+1];
        memcpy(name, data->data, data->data_len);
            name[data->data_len] = 0;

        if (strcmp(name, "PERI_ACC")) {
            return true;
        }
            
        int err = bt_le_scan_stop();
        if (err) {
            LOG_ERR("Stop LE scan failed (err %d)\n", err);
            return true;
        }

        param = BT_LE_CONN_PARAM_DEFAULT;
        err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                    param, &default_conn);
        if (err) {
            LOG_ERR("Create conn failed (err %d)\n", err);
            start_scan();
        }
            
        LOG_INF("Found:  %s\n", name);

        return false;
    }
    return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
             struct net_buf_simple *ad)
{
    char dev[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, dev, sizeof(dev));
    LOG_DBG("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",
           dev, type, ad->len, rssi);

    /* We're only interested in connectable events */
    if (type == BT_GAP_ADV_TYPE_ADV_IND ||
        type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
        bt_data_parse(ad, eir_found, (void *)addr);
    }
}

static void start_scan()
{
    int err;

    /* Use active scanning and disable duplicate filtering to handle any
     * devices that might update their advertising data at runtime. */
    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_ACTIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
    };

    err = bt_le_scan_start(&scan_param, device_found);
    if (err) {
        LOG_ERR("Scanning failed to start (err %d)\n", err);
        return;
    }

    LOG_INF("Scanning successfully started\n");
    enum noti_message msg = BLE_PERIPHERAL_SCANNING;
    send_notification(msg);
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err) {
        LOG_ERR("Failed to connect to %s (%u)\n", addr, conn_err);

        bt_conn_unref(default_conn);
        default_conn = NULL;

        start_scan();
        return;
    }

    LOG_INF("Connected: %s\n", addr);
    enum noti_message msg = BLE_PERIPHERAL_CONNECTED;
    send_notification(msg);

    if (conn == default_conn) {
        memcpy(&uuid, BT_UUID_ACC_SERVICE, sizeof(uuid));
        discover_params.uuid = &uuid.uuid;
        discover_params.func = discover_func;
        discover_params.start_handle = 0x0001;
        discover_params.end_handle = 0xffff;
        discover_params.type = BT_GATT_DISCOVER_PRIMARY;

        err = bt_gatt_discover(default_conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed(err %d)\n", err);
            return;
        }

        /* Increase MTU size from 23 bytes to 160 bytes(defined in prj.conf) 
         * to send 25 samples (2 (16bit) * 3 (axis) * 25 bytes + some overhead) 
         * of accelerometer data
         */  
        exchange_params.func = exchange_func;

        err = bt_gatt_exchange_mtu(conn, &exchange_params);
        if (err) {
            LOG_ERR("MTU exchange failed (err %d)\n", err);
        } else {
            LOG_DBG("MTU exchange pending\n");
        } 

        struct bt_conn_le_data_len_param data_len = {
            .tx_max_len = 251,
            .tx_max_time = (251 + 14) * 8
        };

        err = bt_conn_le_data_len_update(conn, &data_len);
        if (err) {
            LOG_ERR("LE data length update failed: %d", err);
        } else {
            LOG_DBG("LE data length update OK\n");
        }

    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected: %s (reason 0x%02x)\n", addr, reason);
    enum noti_message msg = BLE_PERIPHERAL_DISCONNECTED;
    send_notification(msg);

    if (default_conn != conn) {
        return;
    }

    bt_conn_unref(default_conn);
    default_conn = NULL;

    start_scan();
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

void ble_central_init()
{
    int err = bt_enable(NULL);

    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return;
    }

    LOG_INF("Bluetooth initialized\n");
    bt_conn_cb_register(&conn_callbacks);
    start_scan();
}

