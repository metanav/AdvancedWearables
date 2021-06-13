#include <zephyr.h>
#include "display.h"
#include "ble_central.h"
#include "sd.h"

//struct k_mbox mailbox;
//k_mbox_init(&mailbox);

K_MBOX_DEFINE(data_mailbox);

void main(void)
{
    ble_central_init();
    display_init();

    while (1) {
        k_sleep(K_MSEC(5));
    }
}
