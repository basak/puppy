
/* $Id: usb_io.h,v 1.17 2008/04/10 05:48:02 purbanec Exp $ */

/*

  Copyright (C) 2004-2008 Peter Urbanec <toppy at urbanec.net>

  This file is part of puppy.

  puppy is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  puppy is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with puppy; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef _USB_IO_H
#define _USB_IO_H 1

#include <sys/types.h>
#include <linux/types.h>
#include <linux/version.h>

#include <linux/usbdevice_fs.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include "usb_ch9.h"
#else
#include <linux/usb/ch9.h>
#endif

#include "mjd.h"
#include "tf_bytes.h"

/* Topfield command codes */
#define FAIL                      (0x0001L)
#define SUCCESS                   (0x0002L)
#define CANCEL                    (0x0003L)

#define CMD_READY                 (0x0100L)
#define CMD_RESET                 (0x0101L)
#define CMD_TURBO                 (0x0102L)

#define CMD_HDD_SIZE              (0x1000L)
#define DATA_HDD_SIZE             (0x1001L)

#define CMD_HDD_DIR               (0x1002L)
#define DATA_HDD_DIR              (0x1003L)
#define DATA_HDD_DIR_END          (0x1004L)

#define CMD_HDD_DEL               (0x1005L)
#define CMD_HDD_RENAME            (0x1006L)
#define CMD_HDD_CREATE_DIR        (0x1007L)

#define CMD_HDD_FILE_SEND         (0x1008L)
#define DATA_HDD_FILE_START       (0x1009L)
#define DATA_HDD_FILE_DATA        (0x100aL)
#define DATA_HDD_FILE_END         (0x100bL)

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* Number of milliseconds to wait for a packet transfer to complete. */

/* This is intentionally large enough to allow for a HDD spin up. */
#define TF_PROTOCOL_TIMEOUT 11000


#define trace(level, msg) if(verbose >= level) { msg; }

extern int verbose;

/* 0 - disable tracing
   1 - show packet headers
   2+ - dump entire packet
 */
extern int packet_trace;

extern int ignore_crc;

/* The maximum packet size used by the Toppy.
*/
#define MAXIMUM_PACKET_SIZE 0xFFFFL

/* The size of every packet header. */
#define PACKET_HEAD_SIZE 8

/* Format of a Topfield protocol packet */
struct tf_packet
{
    __u16 length;
    __u16 crc;
    __u32 cmd;
    __u8 data[MAXIMUM_PACKET_SIZE - PACKET_HEAD_SIZE];
} __attribute__ ((packed));

/* Topfield file descriptor data structure. */
struct typefile
{
    struct tf_datetime stamp;
    __u8 filetype;
    __u64 size;
    __u8 name[95];
    __u8 unused;
    __u32 attrib;
} __attribute__ ((packed));


ssize_t send_success(const int fd);
ssize_t send_cancel(const int fd);
ssize_t send_cmd_ready(const int fd);
ssize_t send_cmd_reset(const int fd);
ssize_t send_cmd_turbo(const int fd, const int turbo_on);
ssize_t send_cmd_hdd_size(const int fd);
ssize_t send_cmd_hdd_dir(const int fd, const char *path);
ssize_t send_cmd_hdd_file_send(const int fd, const __u8 dir, const char *path);
ssize_t send_cmd_hdd_del(const int fd, const char *path);
ssize_t send_cmd_hdd_rename(const int fd, const char *src, const char *dst);
ssize_t send_cmd_hdd_create_dir(const int fd, const char *path);

void print_packet(const struct tf_packet *packet, const char *prefix);

ssize_t get_tf_packet(const int fd, struct tf_packet *packet);
ssize_t send_tf_packet(const int fd, struct tf_packet *packet);

ssize_t usb_bulk_read(const int fd, const int ep, const __u8 * bytes,
                      const ssize_t size, const int timeout);
ssize_t usb_bulk_write(const int fd, const int ep, const __u8 * bytes,
                       const ssize_t length, const int timeout);

ssize_t read_device_descriptor(const int fd,
                               struct usb_device_descriptor *desc);
ssize_t read_config_descriptor(const int fd,
                               struct usb_config_descriptor *desc);
ssize_t discard_extra_desc_data(const int fd, struct usb_descriptor_header *desc,
                                ssize_t descSize);

void print_device_descriptor(const struct usb_device_descriptor *desc);
void print_config_descriptor(const struct usb_config_descriptor *desc);

char *decode_error(const struct tf_packet *packet);

#endif /* _USB_IO_H */
