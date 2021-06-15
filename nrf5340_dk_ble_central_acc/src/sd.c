#include <stdio.h>
#include <disk/disk_access.h>
#include <fs/fs.h>
#include <ff.h>
#include "sd.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(sd);

/*
*  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
*  in ffconf.h
*/
static const char *disk_mount_pt = "/SD:";
static long int counter;
static const char counter_file[] = "/SD:/counter.txt";
static FATFS fat_fs;

/* mounting info */
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
};



static int lsdir(const char *path);

bool sd_init()
{
    do { 
        static const char *disk_pdrv = "SD";
        uint64_t memory_size_mb;
        uint32_t block_count;
        uint32_t block_size;
    
        if (disk_access_init(disk_pdrv) != 0) {
            LOG_ERR("Storage init ERROR!");
            return false;
        }
    
        if (disk_access_ioctl(disk_pdrv,
                DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
            LOG_ERR("Unable to get sector count");
            return false;
        }

        LOG_INF("Block count %u", block_count);
    
        if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
            LOG_ERR("Unable to get sector size");
            return false;
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

        struct fs_file_t fp;
        
        // check if file exists
        int ret = fs_open(&fp, counter_file, 0);
        if (ret == 0) {
            fs_close(&fp);
        } 
        if (ret == -2) {
            LOG_INF("File does not exist, creating new one!\n");
            ret = fs_open(&fp, counter_file, FS_O_CREATE | FS_O_WRITE);
            if (ret == 0) {
                char buf[2] = "0";
                fs_write(&fp, &buf, 2);
                fs_close(&fp);
                LOG_INF("Write 0 to the file\n");
            }
        }
 
        ret = fs_open(&fp, counter_file, FS_O_READ);
        
        if (ret == 0) {
            char buf[5];
            fs_read(&fp, buf, 5);
            fs_close(&fp);
            sscanf(buf, "%ld", &counter);
            LOG_INF("counter=%ld\n", counter);
        } else {
            LOG_ERR("counter could not be initialized\n");
            return false;
        }
    } else {
        LOG_ERR("Error mounting disk.\n");
        return false;
    }

    return true;
}


bool write_to_sd(const char *label, int16_t *buf, size_t bufsize)
{
    struct fs_file_t fp;
    char datafile[25];

    // convert first space to underscore
    char *ptr = strchr(label, ' ');
    if (ptr!=NULL) {
        ptr[0] = '_';
    }

    counter += 1;
    
    sprintf(datafile, "/SD:/%s.%ld.csv", label, counter); 
    LOG_INF("counter=%ld, datafile: %s\n", counter, datafile);

    int ret = fs_open(&fp, datafile, FS_O_CREATE | FS_O_WRITE);

    if (ret != 0) {
        LOG_ERR("Failed to open file: %d\n", ret);
        return false;
    }
    
    LOG_ERR("Start writing to %s\n", datafile);
    
    for (int i=0; i<bufsize; i += 3) {
        char line[20];
        ssize_t bufsz = snprintf(line, 20,  "%d,%d,%d\n", buf[i], buf[i+1], buf[i+2]);
        
        //LOG_INF("line=%s", line);

        ret = fs_write(&fp, line, bufsz+1);

        if (ret < 0) {
            LOG_ERR("Failed to write: %d\n", ret);
            return false;
        }
    }
 
    fs_close(&fp);

    ret = fs_open(&fp, counter_file, FS_O_WRITE);
    if (ret == 0) {
        char buf[5];
        ssize_t bufsz = snprintf(buf, 5, "%ld", counter);
        fs_write(&fp, buf, bufsz+1);
        fs_close(&fp);
        LOG_INF("counter=%ld written to the file\n", counter);
    } 
    fs_close(&fp);
    
    LOG_INF("Data written to %s\n", datafile);

    return true;
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
