/*
 * Copyright (c) 2016 Sugizaki Yukimasa (ysugi@idein.jp)
 * All rights reserved.
 *
 * This software is licensed under a Modified (3-Clause) BSD License.
 * You should have received a copy of this license along with this
 * software. If not, contact the copyright holder above.
 */

#ifndef _QMKL_MAILBOX_H_
#define _QMKL_MAILBOX_H_

#include "./raspberrypi-firmware.h"
#include <stdint.h>
#include <sys/types.h>

    int mailbox_open();
    void mailbox_close(int fd_mb);
    void mailbox_property(int fd_mb, void *buf);
    void rpi_firmware_property(const int fd, const uint32_t tag, void *tag_data, const size_t buf_size);
    uint32_t mailbox_qpu_enable(int fd_mb, uint32_t enable);

#endif /* _QMKL_MAILBOX_H_ */
