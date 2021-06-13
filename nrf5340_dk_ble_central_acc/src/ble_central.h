#ifndef __BLE_CENTRAL_H
#define __BLE_CENTRAL_H

#include <zephyr.h>
#include <stdio.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/byteorder.h>

void ble_central_init();

#endif //__BLE_CENTRAL_H
