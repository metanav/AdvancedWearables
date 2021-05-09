#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr.h>
#include <sys/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/byteorder.h>

#include <device.h>
#include <drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#define ACC_SERVICE_UUID 0xd4, 0x86, 0x48, 0x24, 0x54, 0xB3, 0x43, 0xA1, \
                     0xBC, 0x20, 0x97, 0x8F, 0xC3, 0x76, 0xC2, 0x75

#define TX_CHAR_UUID 0xED, 0xAA, 0x20, 0x11, 0x92, 0xE7, 0x43, 0x5A, \
                 0xAA, 0xE9, 0x94, 0x43, 0x35, 0x6A, 0xD4, 0xD3

#define BT_UUID_ACC_SERVICE    BT_UUID_DECLARE_128(ACC_SERVICE_UUID)
#define BT_UUID_ACC_SERVICE_TX BT_UUID_DECLARE_128(TX_CHAR_UUID)

static void start_scan(void);

static struct bt_conn *default_conn;

static struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
lv_obj_t *data_label;

struct chart_type {
    lv_obj_t *chart_obj;
    lv_chart_series_t *series[3]; 
};

// Styles
lv_style_t style_label, style_label_value;


// Fonts
LV_FONT_DECLARE(arial_20bold);
LV_FONT_DECLARE(calibri_20b);
LV_FONT_DECLARE(calibri_20);
LV_FONT_DECLARE(calibri_24b);
LV_FONT_DECLARE(calibri_32b);

static struct bt_gatt_exchange_params exchange_params;

K_MBOX_DEFINE(my_mailbox);
K_SEM_DEFINE(my_sem, 1, 1);

static uint8_t notify_func(struct bt_conn *conn,
               struct bt_gatt_subscribe_params *params,
               const void *payload, uint16_t length)
{
    if (!payload) {
        printk("[UNSUBSCRIBED]\n");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    printk("[NOTIFICATION] payload %p length %u\n", payload, length);
    
    uint8_t *data = (uint8_t*) payload;

    if (length == 2) {
        uint16_t code = data[0] | (data[1] << 8);
        printk("Error: %d\n", code);
    } else { 
        struct k_mbox_msg send_msg = {
            .size = length,
            .tx_data = data,
            .tx_block.data = NULL,
            .tx_target_thread = K_ANY
        };

        k_mbox_async_put(&my_mailbox, &send_msg, NULL);
    }

    return BT_GATT_ITER_CONTINUE;
}

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
                          struct bt_gatt_exchange_params *params)
{
        printk("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");
	//params->func = NULL;
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

    printk("[ATTRIBUTE] handle %u\n", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_ACC_SERVICE)) {
        memcpy(&uuid, BT_UUID_ACC_SERVICE_TX, sizeof(uuid));
        discover_params.uuid = &uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover failed (err %d)\n", err);
        }
    } else if (!bt_uuid_cmp(discover_params.uuid,
                BT_UUID_ACC_SERVICE_TX)) {
        memcpy(&uuid, BT_UUID_GATT_CCC, sizeof(uuid));
        discover_params.uuid = &uuid.uuid;
        discover_params.start_handle = attr->handle + 2;
        discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover failed (err %d)\n", err);
        }
    } else {
        subscribe_params.notify = notify_func;
        subscribe_params.value = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            printk("Subscribe failed (err %d)\n", err);
        } else {
            printk("[SUBSCRIBED] %u\n",  attr->handle);
        }

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
    bt_addr_le_t *addr = user_data;
    printk("[AD]: %u data_len %u\n", data->type, data->data_len);

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
            printk("Stop LE scan failed (err %d)\n", err);
            return true;
        }

        param = BT_LE_CONN_PARAM_DEFAULT;
        err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                    param, &default_conn);
        if (err) {
            printk("Create conn failed (err %d)\n", err);
            start_scan();
        }
            
        printk("Found:  %s\n", name);

        return false;
    }
    return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
             struct net_buf_simple *ad)
{
    char dev[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, dev, sizeof(dev));
    printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",
           dev, type, ad->len, rssi);

    /* We're only interested in connectable events */
    if (type == BT_GAP_ADV_TYPE_ADV_IND ||
        type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
        bt_data_parse(ad, eir_found, (void *)addr);
    }
}

static void start_scan(void)
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
        printk("Scanning failed to start (err %d)\n", err);
        return;
    }

    printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err) {
        printk("Failed to connect to %s (%u)\n", addr, conn_err);

        bt_conn_unref(default_conn);
        default_conn = NULL;

        start_scan();
        return;
    }

    printk("Connected: %s\n", addr);

    if (conn == default_conn) {
        memcpy(&uuid, BT_UUID_ACC_SERVICE, sizeof(uuid));
        discover_params.uuid = &uuid.uuid;
        discover_params.func = discover_func;
        discover_params.start_handle = 0x0001;
        discover_params.end_handle = 0xffff;
        discover_params.type = BT_GATT_DISCOVER_PRIMARY;

        err = bt_gatt_discover(default_conn, &discover_params);
        if (err) {
            printk("Discover failed(err %d)\n", err);
            return;
        }

        /* Increase MTU size from 23 bytes to 160 bytes(defined in prj.conf) 
         * to send 25 samples (2 (16bit) * 3 (axis) * 25 bytes + some overhead) 
         * of accelerometer data
         */  
        exchange_params.func = exchange_func;

        err = bt_gatt_exchange_mtu(conn, &exchange_params);
        if (err) {
            printk("MTU exchange failed (err %d)\n", err);
        } else {
            printk("MTU exchange pending\n");
        } 

        struct bt_conn_le_data_len_param data_len = {
            .tx_max_len = 251,
            .tx_max_time = (251 + 14) * 8
        };

        err = bt_conn_le_data_len_update(conn, &data_len);
        if (err) {
            printk("LE data length update failed: %d", err);
        } else {
            printk("LE data length update OK\n");
        }

    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

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

void style_init()
{
    /*Create background style*/
    static lv_style_t style_screen;
    lv_style_set_bg_color(&style_screen, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x69, 0x69, 0x69));
    lv_obj_add_style(lv_scr_act(), LV_BTN_PART_MAIN, &style_screen);
    
    /* Create a label value style */
    lv_style_init(&style_label_value);
    lv_style_set_bg_opa(&style_label_value, LV_STATE_DEFAULT, LV_OPA_20);
    lv_style_set_bg_color(&style_label_value, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_grad_color(&style_label_value, LV_STATE_DEFAULT, LV_COLOR_TEAL);
    lv_style_set_bg_grad_dir(&style_label_value, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_style_set_pad_left(&style_label_value, LV_STATE_DEFAULT, 0);
    lv_style_set_pad_top(&style_label_value, LV_STATE_DEFAULT, 3);
    
    /* Set the text style */
    lv_style_set_text_color(&style_label_value, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x00, 0x00, 0x30));
    lv_style_set_text_font(&style_label_value, LV_STATE_DEFAULT, &calibri_20);

    //data_label = lv_label_create(lv_scr_act(), NULL);
    //lv_obj_align(data_label, NULL, LV_ALIGN_CENTER, -35, 0);
    //lv_obj_add_style(data_label, LV_LABEL_PART_MAIN, &style_label_value);
}

void chart_init(struct chart_type *chart)
{   
    /* Create a chart */
    chart->chart_obj = lv_chart_create(lv_scr_act(), NULL);
    lv_obj_set_size(chart->chart_obj, 300, 200);
    lv_obj_align(chart->chart_obj, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_chart_set_type(chart->chart_obj, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
    
    /*Add two data series*/
    chart->series[0] = lv_chart_add_series(chart->chart_obj, LV_COLOR_RED);
    chart->series[1] = lv_chart_add_series(chart->chart_obj, LV_COLOR_GREEN);
    chart->series[2] = lv_chart_add_series(chart->chart_obj, LV_COLOR_BLUE);
}

void chart_update(struct chart_type *chart, uint16_t *data, bool refresh)
{
    lv_chart_set_next(chart->chart_obj, chart->series[0], data[0]);
    lv_chart_set_next(chart->chart_obj, chart->series[1], data[1]);
    lv_chart_set_next(chart->chart_obj, chart->series[2], data[2]);
    if (refresh) {
        lv_chart_refresh(chart->chart_obj);
    }
}

void main(void)
{
    int err = bt_enable(NULL);

    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");
    bt_conn_cb_register(&conn_callbacks);
    start_scan();

    const struct device *display_dev;
    display_dev = device_get_binding(CONFIG_LVGL_DISPLAY_DEV_NAME);

    if (display_dev == NULL) {
        printk("%s device not found.  Aborting test.", CONFIG_LVGL_DISPLAY_DEV_NAME);
        return;
    }

    printk("Display %s initialized\n", CONFIG_LVGL_DISPLAY_DEV_NAME);
    display_blanking_off(display_dev);

    struct chart_type acc_chart;
    uint16_t data[3];
    
    chart_init(&acc_chart);
    uint8_t count = 0;
    bool refresh;
    struct k_mbox_msg recv_msg;
    uint8_t buffer[150];
    int16_t x, y, z;

    while (1) {
        refresh = false;
        recv_msg.size = 150;
        recv_msg.rx_source_thread = K_ANY;

        k_mbox_get(&my_mailbox, &recv_msg, buffer, K_FOREVER);

        printf("size=%d\n", recv_msg.size);

        for (uint8_t i = 0; i < 150; i += 6) {
            x = buffer[i+0] | (buffer[i+1] << 8);
            y = buffer[i+2] | (buffer[i+3] << 8);
            z = buffer[i+4] | (buffer[i+5] << 8);

            printf("%d, %d, %d\n", x, y, z);
            data[0] = 100 + ((x-1200)/24.f);
            data[1] = 100 + ((y-1200)/24.f);
            data[2] = 100 + ((z-1200)/24.f);
        
            if (++count == 25) {
                count = 0;
                refresh = true;
            }
            chart_update(&acc_chart, data, refresh);
        }
        lv_task_handler();
        //k_sleep(K_MSEC(5));
    }
}
