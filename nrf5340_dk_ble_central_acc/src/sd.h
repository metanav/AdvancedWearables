#ifndef __SD_H
#define __SD_H

#include <zephyr/types.h>
#include <errno.h>
#include <zephyr.h>
 
bool sd_init();
bool write_to_sd(const char *label, int16_t *buf, size_t bufsize);

#endif //__SD_H
