#ifndef __SD_H
#define __SD_H

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr.h>

#include <disk/disk_access.h>
#include <fs/fs.h>
#include <ff.h>

void sd_init();
void write_to_sd(char *buf, size_t bufsize);

#endif //__SD_H
