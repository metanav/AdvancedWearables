#include <zephyr.h>
#include "display.h"
#include "ble_central.h"


/* define notification mailbox 
 * to inform the threads
 */
K_MBOX_DEFINE(notification_mailbox);

/* define dedicated mailbox to forward data received from BLE peripheral to 
 * the display_thread
 */
K_MBOX_DEFINE(data_mailbox);

void main(void)
{
    ble_central_init();
    // the edge impulse inferencing code is in the display.cpp file
    display_init();

    while (1) {
        k_sleep(K_MSEC(1000));
    }
}

/* display thread
 * stack size = 8192 to hold thread variables 
 */
K_THREAD_DEFINE(display_thread, 8192, display_entrypoint, NULL, NULL, NULL, 7, 0, 0);
