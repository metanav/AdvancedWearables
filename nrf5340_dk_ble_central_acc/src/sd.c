#include "sd.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(sd);

/*
*  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
*  in ffconf.h
*/
static const char *disk_mount_pt = "/SD:";

static FATFS fat_fs;

/* mounting info */
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
};

static int lsdir(const char *path);

void sd_init()
{
    do { 
        static const char *disk_pdrv = "SD";
        uint64_t memory_size_mb;
        uint32_t block_count;
        uint32_t block_size;
    
        if (disk_access_init(disk_pdrv) != 0) {
            printk("Storage init ERROR!");
            break;
        }
    
        if (disk_access_ioctl(disk_pdrv,
                DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
            printk("Unable to get sector count");
            break;
        }

        LOG_INF("Block count %u", block_count);
    
        if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
            LOG_ERR("Unable to get sector size");
            break;
        }

        LOG_INF("Sector size %u\n", block_size);
    
        memory_size_mb = (uint64_t)block_count * block_size;

        LOG_INF("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));
    } while (0);

    mp.mnt_point = disk_mount_pt;
    int res = fs_mount(&mp);
    
    if (res == FR_OK) {
        LOG_INF("Disk mounted.\n");
        lsdir(disk_mount_pt);
    } else {
        LOG_ERR("Error mounting disk.\n");
    }
}


void write_to_sd(char *buf, size_t bufsize)
{
    struct fs_file_t fp;

    int ret = fs_open(&fp, "/SD:/new.txt", FS_O_CREATE | FS_O_WRITE);

    if (ret != 0) {
        LOG_ERR("Failed to open file: %d\n", ret);
        return;
    }
    
    ret = fs_write(&fp, buf, bufsize);

    if (ret < 0) {
        LOG_ERR("Failed to write: %d\n", ret);
        return;
    }

    fs_close(&fp);
}

static int lsdir(const char *path)
{
        int res;
        struct fs_dir_t dirp;
        static struct fs_dirent entry;

        /* Verify fs_opendir() */
        res = fs_opendir(&dirp, path);
        if (res) {
            LOG_ERR("Error opening dir %s [%d]\n", path, res);
            return res;
        }

        LOG_INF("\nListing dir %s ...\n", path);
        for (;;) {
            /* Verify fs_readdir() */
            res = fs_readdir(&dirp, &entry);

            /* entry.name[0] == 0 means end-of-dir */
            if (res || entry.name[0] == 0) {
                break;
            }

            if (entry.type == FS_DIR_ENTRY_DIR) {
                LOG_INF("[DIR ] %s\n", entry.name);
            } else {
                LOG_INF("[FILE] %s (size = %zu)\n", entry.name, entry.size);
            }
        }

        /* Verify fs_closedir() */
        fs_closedir(&dirp);

        return res;
}
