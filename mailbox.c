/*
 * Copyright (c) 2016 Sugizaki Yukimasa (ysugi@idein.jp)
 * All rights reserved.
 *
 * This software is licensed under a Modified (3-Clause) BSD License.
 * You should have received a copy of this license along with this
 * software. If not, contact the copyright holder above.
 */

#include "./error.h"
#include "./mailbox.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#define DEVICE_PREFIX "/dev/"
#define DEVICE_FILE_NAME "vcio"

/* c.f. https://github.com/raspberrypi/linux/blob/rpi-4.4.y/drivers/char/broadcom/vcio.c */
#define VCIO_IOC_MAGIC 100
#define IOCTL_MBOX_PROPERTY _IOWR(VCIO_IOC_MAGIC, 0, char*)

int mailbox_open()
{
    int fd_mb;

    fd_mb = open(DEVICE_PREFIX DEVICE_FILE_NAME, O_NONBLOCK);
    if (fd_mb == -1)
        error_fatal("Failed to open %s: %s\n", DEVICE_PREFIX DEVICE_FILE_NAME, strerror(errno));

    return fd_mb;
}

void mailbox_close(int fd_mb)
{
    int ret_int;

    ret_int = close(fd_mb);
    if (ret_int == -1)
        error_fatal("Failed to close %s: %s\n", DEVICE_PREFIX DEVICE_FILE_NAME, strerror(errno));
}

void mailbox_property(int fd_mb, void *buf)
{
    int ret_int;

    ret_int = ioctl(fd_mb, IOCTL_MBOX_PROPERTY, buf);
    if (ret_int == -1)
        error_fatal("ioctl: %s\n", strerror(errno));
}

static void check_error(uint32_t p[])
{
    switch (p[1]) {
        case RPI_FIRMWARE_STATUS_SUCCESS:
            break;
        case RPI_FIRMWARE_STATUS_ERROR:
            error_fatal("Mailbox process request failed\n");
            break;
        default:
            error_fatal("Unknown Mailbox return code: 0x%08x\n", p[1]);
    }
}

void rpi_firmware_property(const int fd, const uint32_t tag, void *tag_data, const size_t buf_size)
{
    int i;
    uint32_t *p = NULL;

    p = malloc((5 + buf_size / 4 + 1) * sizeof(*p));
    if (p == NULL)
        error_fatal("Failed to allocate memory for RPi firmware\n");

    i = 0;
    p[i++] = (5 + buf_size / 4 + 1) * sizeof(*p);
    p[i++] = RPI_FIRMWARE_STATUS_REQUEST;
    p[i++] = tag; // tag
    p[i++] = buf_size; // buf_size
    p[i++] = buf_size; // req_resp_size
    memcpy(p + i, tag_data, buf_size);
    p[i + buf_size / 4] = RPI_FIRMWARE_PROPERTY_END;;

    mailbox_property(fd, p);
    check_error(p);
    memcpy(tag_data, p + i, buf_size);
    free(p);
}

uint32_t mailbox_qpu_enable(int fd_mb, uint32_t enable)
{
    struct {
        union {
            uint32_t enable, ret;
        };
    } gpu = {
        .enable = enable
    };

    rpi_firmware_property(fd_mb, RPI_FIRMWARE_SET_ENABLE_QPU, &gpu, sizeof(gpu));
    return gpu.ret;
}
