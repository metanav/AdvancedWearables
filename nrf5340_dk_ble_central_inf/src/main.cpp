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
    display_init();

    while (1) {
        k_sleep(K_MSEC(1000));
    }
}

/* display thread
 * stack size = 12288 to hold thread variables 
 * and 9K fatfs write buffer for 60 seconds of 
 * 25Hz 3-axis accelerometer data
 */
K_THREAD_DEFINE(display_thread, 12288, display_entrypoint, NULL, NULL, NULL, 7, 0, 0);
