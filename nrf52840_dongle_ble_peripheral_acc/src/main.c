#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/printk.h>
#include <sys/byteorder.h>
#include <zephyr.h>
#include <drivers/gpio.h>
#include <soc.h>
#include <device.h>
#include <drivers/sensor.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <dk_buttons_and_leds.h>

#include "acc_service.h"

#define CON_STATUS_LED DK_LED2

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)

static K_SEM_DEFINE(ble_init_ok, 0, 1);

static const struct bt_data ad[] = 
{
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = 
{
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, ACC_SERVICE_UUID),
};

struct bt_conn *ble_connection;

static void connected(struct bt_conn *conn, u8_t err)
{
    struct bt_conn_info info; 
    char addr[BT_ADDR_LE_STR_LEN];

    ble_connection = conn;

    if (err) {
        printk("Connection failed (err %u)\n", err);
        return;
    } else if(bt_conn_get_info(conn, &info)) {
        printk("Could not parse connection info\n");
    }
    else {
        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        dk_set_led_on(CON_STATUS_LED);
        
        printk("Connection established!        \n\
        Connected to: %s                    \n\
        Role: %u                            \n\
        Connection interval: %u                \n\
        Slave latency: %u                    \n\
        Connection supervisory timeout: %u    \n"
        , addr, info.role, info.le.interval, info.le.latency, info.le.timeout);
    }
}

static void disconnected(struct bt_conn *conn, u8_t reason)
{
    dk_set_led_off(CON_STATUS_LED);
    printk("Disconnected (reason %u)\n", reason);
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
    //If acceptable params, return true, otherwise return false.
    return true; 
}

static void le_param_updated(struct bt_conn *conn, u16_t interval, u16_t latency, u16_t timeout)
{
    struct bt_conn_info info; 
    char addr[BT_ADDR_LE_STR_LEN];
    
    if(bt_conn_get_info(conn, &info))
    {
        printk("Could not parse connection info\n");
    }
    else
    {
        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        
        printk("Connection parameters updated!    \n\
        Connected to: %s                        \n\
        New Connection Interval: %u                \n\
        New Slave Latency: %u                    \n\
        New Connection Supervisory Timeout: %u    \n"
        , addr, info.le.interval, info.le.latency, info.le.timeout);
    }
}

static struct bt_conn_cb conn_callbacks = 
{
    .connected                = connected,
    .disconnected           = disconnected,
    .le_param_req            = le_param_req,
    .le_param_updated        = le_param_updated
};

static void bt_ready(int err)
{
    if (err) 
    {
        printk("BLE init failed with error code %d\n", err);
        return;
    }

    //Configure connection callbacks
    bt_conn_cb_register(&conn_callbacks);

    //Initalize services
    err = acc_service_init();

    if (err) 
    {
        printk("Failed to init LBS (err:%d)\n", err);
        return;
    }

    //Start advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
                  sd, ARRAY_SIZE(sd));
    if (err) 
    {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising successfully started\n");

    k_sem_give(&ble_init_ok);
}


static void error(uint8_t code)
{
    while (true) {
        printk("Error!\n");
        if (code > 0) {
            acc_service_send(ble_connection, (uint8_t*) &code, 1);
        }
        /* Spin for ever */
        k_sleep(K_MSEC(1000)); //1000ms
    }
}

void main(void)
{
    
    int err = 0;

    err = dk_leds_init();
    if (err) {
        printk("Cannot init LEDs (err: %d)", err);
        error(0);
    }

    printk("Starting Nordic BLE peripheral tutorial\n");

    
    err = bt_enable(bt_ready);

    if (err) 
    {
        printk("BLE initialization failed\n");
        error(0); //Catch error
    }
    
    /**  
      Bluetooth stack should be ready in less than 100 msec.
      We use this semaphore to wait for bt_enable to call bt_ready before we proceed 
      to the main loop. By using the semaphore to block execution we allow the RTOS to
      execute other tasks while we wait. 
    **/   
 
    err = k_sem_take(&ble_init_ok, K_MSEC(500));

    if (!err) {
        printk("Bluetooth initialized\n");
    } else {
        printk("BLE initialization did not complete in time\n");
        error(0); //Catch error
    }

    err = acc_service_init();

    struct sensor_value accel[3];

    const struct device *dev = device_get_binding(DT_LABEL(DT_INST(0, adi_adxl345)));
    if (dev == NULL) {
        printk("Device get binding device\n");
        error(1); //Catch error
    }

    
    for (;;) 
    {
        k_sleep(K_MSEC(1000));

        if (sensor_sample_fetch(dev) < 0) {
            printk("Sample fetch error\n");
            error(2); //Catch error
        }

        if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]) < 0) {
            printk("Channel get error\n");
            error(3); //Catch error
        }

        if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]) < 0) {
            printk("Channel get error\n");
            error(3); //Catch error
        }

        if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]) < 0) {
            printk("Channel get error\n");
            error(3); //Catch error
        }

        uint16_t data[3] = { accel[0].val1, accel[1].val1, accel[2].val1 };

        acc_service_send(ble_connection, (uint8_t *) data, 6); 
        //uint8_t str[15] = {0};
        //sprintf(str, "%d,%d,%d,", accel[0].val1, accel[1].val1, accel[2].val1);
        //acc_service_send(ble_connection, (uint8_t*) str, 15); 
       
    }
}
