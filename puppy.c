
/* $Id: puppy.c,v 1.25 2008/04/10 05:48:02 purbanec Exp $ */

/* Format using indent and the following options:
-bad -bap -bbb -i4 -bli0 -bl0 -cbi0 -cli4 -ss -npcs -nprs -nsaf -nsai -nsaw -nsc -nfca -nut -lp -npsl
*/

/*

  Copyright (C) 2004-2008 Peter Urbanec <toppy at urbanec.net>
  Copyright (C) 2009 Mark Colclough <m.s.colclough bham.ac.uk> (findToppy)

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

#define PUPPY_RELEASE "1.14"

#define _LARGEFILE64_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <asm/byteorder.h>
#include <dirent.h>

#include "usb_io.h"
#include "tf_bytes.h"

#define PUT 0
#define GET 1

#define SYSPATH_MAX 256
#define TOPPYVID 0x11db
#define TOPPYPID 0x1000

extern time_t timezone;

int lockFd = -1;
int quiet = 0;
char *devPath = NULL;
__u32 cmd = 0;
char *arg1 = NULL;
char *arg2 = NULL;
__u8 sendDirection = GET;
struct tf_packet packet;
struct tf_packet reply;

int parseArgs(int argc, char *argv[]);
int isToppy(struct usb_device_descriptor *desc);
char *findToppy(void);
int do_cancel(int fd);
int do_cmd_ready(int fd);
int do_cmd_reset(int fd);
int do_hdd_size(int fd);
int do_hdd_dir(int fd, char *path);
int do_hdd_file_put(int fd, char *srcPath, char *dstPath);
int do_hdd_file_get(int fd, char *srcPath, char *dstPath);
void decode_dir(struct tf_packet *p);
int do_hdd_del(int fd, char *path);
int do_hdd_rename(int fd, char *srcPath, char *dstPath);
int do_hdd_mkdir(int fd, char *path);
int do_cmd_turbo(int fd, char *state);
void progressStats(__u64 totalSize, __u64 bytes, time_t startTime);
void finalStats(__u64 bytes, time_t startTime);

#define E_INVALID_ARGS 1
#define E_READ_DEVICE 2
#define E_NOT_TF5000PVR 3
#define E_SET_INTERFACE 4
#define E_CLAIM_INTERFACE 5
#define E_DEVICE_LOCK 6
#define E_LOCK_FILE 7
#define E_GLOBAL_LOCK 8
#define E_RESET_DEVICE 9

int main(int argc, char *argv[])
{
    struct usb_device_descriptor devDesc;
    int fd = -1;
    int r;

    /* Initialise timezone handling. */
    tzset();

    lockFd = open("/tmp/puppy", O_CREAT, S_IRUSR | S_IWUSR);
    if(lockFd < 0)
    {
        fprintf(stderr, "ERROR: Can not open lock file /tmp/puppy: %s\n",
                strerror(errno));
        return E_LOCK_FILE;
    }

    r = parseArgs(argc, argv);
    if(r != 0)
    {
        return E_INVALID_ARGS;
    }

    /* Create a lock, so that other instances of puppy can detect this one. */
    if(0 != flock(lockFd, LOCK_SH | LOCK_NB))
    {
        fprintf(stderr,
                "ERROR: Can not obtain shared lock on /tmp/puppy: %s\n",
                strerror(errno));
        return E_GLOBAL_LOCK;
    }

    trace(2, fprintf(stderr, "cmd %04x on %s\n", cmd, devPath));

    fd = open(devPath, O_RDWR);
    if(fd < 0)
    {
        fprintf(stderr, "ERROR: Can not open %s for read/write: %s\n",
                devPath, strerror(errno));
        return E_READ_DEVICE;
    }

    if(0 != flock(fd, LOCK_EX | LOCK_NB))
    {
        fprintf(stderr, "ERROR: Can not get exclusive lock on %s\n", devPath);
        close(fd);
        return E_DEVICE_LOCK;
    }

    r = read_device_descriptor(fd, &devDesc);
    if(r < 0)
    {
        close(fd);
        return E_READ_DEVICE;
    }

    if(!isToppy(&devDesc))
    {
        fprintf(stderr, "ERROR: Could not find a Topfield TF5000PVRt\n");
        close(fd);
        return E_NOT_TF5000PVR;
    }

    trace(1, fprintf(stderr, "Found a Topfield TF5000PVRt\n"));

    trace(2, fprintf(stderr, "USBDEVFS_RESET\n"));
    r = ioctl(fd, USBDEVFS_RESET, NULL);
    if(r < 0)
    {
        fprintf(stderr, "ERROR: Can not reset device: %s\n", strerror(errno));
        close(fd);
        return E_RESET_DEVICE;
    }

    {
        int interface = 0;

        trace(2, fprintf(stderr, "USBDEVFS_CLAIMINTERFACE\n"));
        r = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface);
        if(r < 0)
        {
            fprintf(stderr, "ERROR: Can not claim interface 0: %s\n",
                    strerror(errno));
            close(fd);
            return E_CLAIM_INTERFACE;
        }
    }

    {
        struct usbdevfs_setinterface interface0 = { 0, 0 };

        trace(2, fprintf(stderr, "USBDEVFS_SETNTERFACE\n"));
        r = ioctl(fd, USBDEVFS_SETINTERFACE, &interface0);
        if(r < 0)
        {
            fprintf(stderr, "ERROR: Can not set interface zero: %s\n",
                    strerror(errno));
            close(fd);
            return E_SET_INTERFACE;
        }
    }

    switch (cmd)
    {
        case CANCEL:
            r = do_cancel(fd);
            break;

        case CMD_RESET:
            r = do_cmd_reset(fd);
            break;

        case CMD_HDD_SIZE:
            r = do_hdd_size(fd);
            break;

        case CMD_HDD_DIR:
            r = do_hdd_dir(fd, arg1);
            break;

        case CMD_HDD_FILE_SEND:
            if(sendDirection == PUT)
            {
                r = do_hdd_file_put(fd, arg1, arg2);
            }
            else
            {
                r = do_hdd_file_get(fd, arg1, arg2);
            }
            break;

        case CMD_HDD_DEL:
            r = do_hdd_del(fd, arg1);
            break;

        case CMD_HDD_RENAME:
            r = do_hdd_rename(fd, arg1, arg2);
            break;

        case CMD_HDD_CREATE_DIR:
            r = do_hdd_mkdir(fd, arg1);
            break;

        case CMD_TURBO:
            r = do_cmd_turbo(fd, arg1);
            break;

        default:
            fprintf(stderr, "BUG: Command 0x%08x not implemented\n", cmd);
            r = -EINVAL;
    }

    {
        int interface = 0;

        ioctl(fd, USBDEVFS_RELEASEINTERFACE, &interface);
        close(fd);
    }
    return r;
}

int do_cmd_turbo(int fd, char *state)
{
    int r;
    int turbo_on = atoi(state);

    if(0 == strcasecmp("ON", state))
    {
        turbo_on = 1;
    }

    r = send_cmd_turbo(fd, turbo_on);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }

    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            trace(1,
                  fprintf(stderr, "Turbo mode: %s\n",
                          turbo_on ? "ON" : "OFF"));
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

int do_cmd_reset(int fd)
{
    int r;

    r = send_cmd_reset(fd);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }

    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            printf("TF5000PVRt should now reboot\n");
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

int do_cmd_ready(int fd)
{
    int r;

    r = send_cmd_ready(fd);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }

    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            printf("Device reports ready.\n");
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            get_u32(&reply.data);
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
            return -1;
    }
    return -EPROTO;
}

int do_cancel(int fd)
{
    int r;

    r = send_cancel(fd);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }

    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            printf("In progress operation cancelled\n");
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

int do_hdd_size(int fd)
{
    int r;

    r = send_cmd_hdd_size(fd);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }

    switch (get_u32(&reply.cmd))
    {
        case DATA_HDD_SIZE:
        {
            __u32 totalk = get_u32(&reply.data);
            __u32 freek = get_u32(&reply.data[4]);

            printf("Total %10u kiB %7u MiB %4u GiB\n", totalk, totalk / 1024,
                   totalk / (1024 * 1024));
            printf("Free  %10u kiB %7u MiB %4u GiB\n", freek, freek / 1024,
                   freek / (1024 * 1024));
            return 0;
            break;
        }

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

int do_hdd_dir(int fd, char *path)
{
    int r;

    r = send_cmd_hdd_dir(fd, path);
    if(r < 0)
    {
        return -EPROTO;
    }

    while(0 < get_tf_packet(fd, &reply))
    {
        switch (get_u32(&reply.cmd))
        {
            case DATA_HDD_DIR:
                decode_dir(&reply);
                send_success(fd);
                break;

            case DATA_HDD_DIR_END:
                return 0;
                break;

            case FAIL:
                fprintf(stderr, "ERROR: Device reports %s\n",
                        decode_error(&reply));
                return -EPROTO;
                break;

            default:
                fprintf(stderr, "ERROR: Unhandled packet\n");
                return -EPROTO;
        }
    }
    return -EPROTO;
}

void decode_dir(struct tf_packet *p)
{
    __u16 count =
        (get_u16(&p->length) - PACKET_HEAD_SIZE) / sizeof(struct typefile);
    struct typefile *entries = (struct typefile *) p->data;
    int i;
    time_t timestamp;

    for(i = 0; (i < count); i++)
    {
        char type;

        switch (entries[i].filetype)
        {
            case 1:
                type = 'd';
                break;

            case 2:
                type = 'f';
                break;

            default:
                type = '?';
        }

        /* This makes the assumption that the timezone of the Toppy and the system
         * that puppy runs on are the same. Given the limitations on the length of
         * USB cables, this condition is likely to be satisfied. */
        timestamp = tfdt_to_time(&entries[i].stamp);
        printf("%c %20llu %24.24s %s\n", type, get_u64(&entries[i].size),
               ctime(&timestamp), entries[i].name);
    }
}

int do_hdd_file_put(int fd, char *srcPath, char *dstPath)
{
    int result = -EPROTO;
    time_t startTime = time(NULL);
    enum
    {
        START,
        DATA,
        END,
        FINISHED
    } state;
    int src = -1;
    int r;
    int update = 0;
    struct stat64 srcStat;
    __u64 fileSize;
    __u64 byteCount = 0;

    trace(4, fprintf(stderr, "%s\n", __func__));

    src = open64(srcPath, O_RDONLY);
    if(src < 0)
    {
        fprintf(stderr, "ERROR: Can not open source file: %s\n",
                strerror(errno));
        return errno;
    }

    if(0 != fstat64(src, &srcStat))
    {
        fprintf(stderr, "ERROR: Can not examine source file: %s\n",
                strerror(errno));
        result = errno;
        goto out;
    }

    fileSize = srcStat.st_size;
    if(fileSize == 0)
    {
        fprintf(stderr, "ERROR: Source file is empty - not transfering.\n");
        result = -ENODATA;
        goto out;
    }

    r = send_cmd_hdd_file_send(fd, PUT, dstPath);
    if(r < 0)
    {
        goto out;
    }

    state = START;
    while(0 < get_tf_packet(fd, &reply))
    {
        update = (update + 1) % 16;
        switch (get_u32(&reply.cmd))
        {
            case SUCCESS:
                switch (state)
                {
                    case START:
                    {
                        /* Send start */
                        struct typefile *tf = (struct typefile *) packet.data;

                        put_u16(&packet.length, PACKET_HEAD_SIZE + 114);
                        put_u32(&packet.cmd, DATA_HDD_FILE_START);
                        time_to_tfdt(srcStat.st_mtime, &tf->stamp);
                        tf->filetype = 2;
                        put_u64(&tf->size, srcStat.st_size);
                        strncpy((char *) tf->name, dstPath, 94);
                        tf->name[94] = '\0';
                        tf->unused = 0;
                        tf->attrib = 0;
                        trace(3,
                              fprintf(stderr, "%s: DATA_HDD_FILE_START\n",
                                      __func__));
                        r = send_tf_packet(fd, &packet);
                        if(r < 0)
                        {
                            fprintf(stderr, "ERROR: Incomplete send.\n");
                            goto out;
                        }
                        state = DATA;
                        break;
                    }

                    case DATA:
                    {
                        int payloadSize = sizeof(packet.data) - 9;
                        ssize_t w = read(src, &packet.data[8], payloadSize);

                        /* Detect a Topfield protcol bug and prevent the sending of packets
                           that are a multiple of 512 bytes. */
                        if((w > 4)
                           &&
                           (((((PACKET_HEAD_SIZE + 8 + w) +
                               1) & ~1) % 0x200) == 0))
                        {
                            lseek64(src, -4, SEEK_CUR);
                            w -= 4;
                            payloadSize -= 4;
                        }

                        put_u16(&packet.length, PACKET_HEAD_SIZE + 8 + w);
                        put_u32(&packet.cmd, DATA_HDD_FILE_DATA);
                        put_u64(packet.data, byteCount);
                        byteCount += w;

                        /* Detect EOF and transition to END */
                        if((w < 0) || (byteCount >= fileSize))
                        {
                            state = END;
                        }

                        if(w > 0)
                        {
                            trace(3,
                                  fprintf(stderr, "%s: DATA_HDD_FILE_DATA\n",
                                          __func__));
                            r = send_tf_packet(fd, &packet);
                            if(r < w)
                            {
                                fprintf(stderr, "ERROR: Incomplete send.\n");
                                goto out;
                            }
                        }

                        if(!update && !quiet)
                        {
                            progressStats(fileSize, byteCount, startTime);
                        }
                        break;
                    }

                    case END:
                        /* Send end */
                        put_u16(&packet.length, PACKET_HEAD_SIZE);
                        put_u32(&packet.cmd, DATA_HDD_FILE_END);
                        trace(3,
                              fprintf(stderr, "%s: DATA_HDD_FILE_END\n",
                                      __func__));
                        r = send_tf_packet(fd, &packet);
                        if(r < 0)
                        {
                            fprintf(stderr, "ERROR: Incomplete send.\n");
                            goto out;
                        }
                        state = FINISHED;
                        break;

                    case FINISHED:
                        result = 0;
                        goto out;
                        break;
                }
                break;

            case FAIL:
                fprintf(stderr, "ERROR: Device reports %s\n",
                        decode_error(&reply));
                goto out;
                break;

            default:
                fprintf(stderr, "ERROR: Unhandled packet\n");
                break;
        }
    }
    finalStats(byteCount, startTime);

  out:
    close(src);
    return result;
}

int do_hdd_file_get(int fd, char *srcPath, char *dstPath)
{
    int result = -EPROTO;
    time_t startTime = time(NULL);
    enum
    {
        START,
        DATA,
        ABORT
    } state;
    int dst = -1;
    int r;
    int update = 0;
    __u64 byteCount = 0;
    struct utimbuf mod_utime_buf = { 0, 0 };

    dst = open64(dstPath, O_WRONLY | O_CREAT | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if(dst < 0)
    {
        fprintf(stderr, "ERROR: Can not open destination file: %s\n",
                strerror(errno));
        return errno;
    }

    r = send_cmd_hdd_file_send(fd, GET, srcPath);
    if(r < 0)
    {
        goto out;
    }

    state = START;
    while(0 < (r = get_tf_packet(fd, &reply)))
    {
        update = (update + 1) % 16;
        switch (get_u32(&reply.cmd))
        {
            case DATA_HDD_FILE_START:
                if(state == START)
                {
                    struct typefile *tf = (struct typefile *) reply.data;

                    byteCount = get_u64(&tf->size);
                    mod_utime_buf.actime = mod_utime_buf.modtime =
                        tfdt_to_time(&tf->stamp);

                    send_success(fd);
                    state = DATA;
                }
                else
                {
                    fprintf(stderr,
                            "ERROR: Unexpected DATA_HDD_FILE_START packet in state %d\n",
                            state);
                    send_cancel(fd);
                    state = ABORT;
                }
                break;

            case DATA_HDD_FILE_DATA:
                if(state == DATA)
                {
                    __u64 offset = get_u64(reply.data);
                    __u16 dataLen =
                        get_u16(&reply.length) - (PACKET_HEAD_SIZE + 8);
                    ssize_t w;

                    if(!update && !quiet)
                    {
                        progressStats(byteCount, offset + dataLen, startTime);
                    }

                    if(r < get_u16(&reply.length))
                    {
                        fprintf(stderr,
                                "ERROR: Short packet %d instead of %d\n", r,
                                get_u16(&reply.length));
                        /* TODO: Fetch the rest of the packet */
                    }

                    w = write(dst, &reply.data[8], dataLen);
                    if(w < dataLen)
                    {
                        /* Can't write data - abort transfer */
                        fprintf(stderr, "ERROR: Can not write data: %s\n",
                                strerror(errno));
                        send_cancel(fd);
                        state = ABORT;
                    }
                }
                else
                {
                    fprintf(stderr,
                            "ERROR: Unexpected DATA_HDD_FILE_DATA packet in state %d\n",
                            state);
                    send_cancel(fd);
                    state = ABORT;
                }
                break;

            case DATA_HDD_FILE_END:
                send_success(fd);
                result = 0;
                goto out;
                break;

            case FAIL:
                fprintf(stderr, "ERROR: Device reports %s\n",
                        decode_error(&reply));
                send_cancel(fd);
                state = ABORT;
                break;

            case SUCCESS:
                goto out;
                break;

            default:
                fprintf(stderr, "ERROR: Unhandled packet (cmd 0x%x)\n",
                        get_u32(&reply.cmd));
        }
    }
    utime(dstPath, &mod_utime_buf);
    finalStats(byteCount, startTime);

  out:
    close(dst);
    return result;
}

int do_hdd_del(int fd, char *path)
{
    int r;

    r = send_cmd_hdd_del(fd, path);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }
    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

int do_hdd_rename(int fd, char *srcPath, char *dstPath)
{
    int r;

    r = send_cmd_hdd_rename(fd, srcPath, dstPath);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }
    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

int do_hdd_mkdir(int fd, char *path)
{
    int r;

    r = send_cmd_hdd_create_dir(fd, path);
    if(r < 0)
    {
        return -EPROTO;
    }

    r = get_tf_packet(fd, &reply);
    if(r < 0)
    {
        return -EPROTO;
    }
    switch (get_u32(&reply.cmd))
    {
        case SUCCESS:
            return 0;
            break;

        case FAIL:
            fprintf(stderr, "ERROR: Device reports %s\n",
                    decode_error(&reply));
            break;

        default:
            fprintf(stderr, "ERROR: Unhandled packet\n");
    }
    return -EPROTO;
}

void progressStats(__u64 totalSize, __u64 bytes, time_t startTime)
{
    int delta = time(NULL) - startTime;

    if(quiet)
        return;

    if(delta > 0)
    {
        double rate = bytes / delta;
        int eta = (totalSize - bytes) / rate;

        fprintf(stderr,
                "\r%6.2f%%, %5.2f Mbits/s, %02d:%02d:%02d elapsed, %02d:%02d:%02d remaining",
                100.0 * ((double) (bytes) / (double) totalSize),
                ((bytes * 8.0) / delta) / (1000 * 1000), delta / (60 * 60),
                (delta / 60) % 60, delta % 60, eta / (60 * 60),
                (eta / 60) % 60, eta % 60);
    }
}

void finalStats(__u64 bytes, time_t startTime)
{
    int delta = time(NULL) - startTime;

    if(quiet)
        return;

    if(delta > 0)
    {
        fprintf(stderr, "\n%.2f Mbytes in %02d:%02d:%02d (%.2f Mbits/s)\n",
                (double) bytes / (1000.0 * 1000.0),
                delta / (60 * 60), (delta / 60) % 60, delta % 60,
                ((bytes * 8.0) / delta) / (1000.0 * 1000.0));
    }
}

void usage(char *myName)
{
    char *usageString =
        "Usage: %s [-ipPqv] [-d <device>] -c <command> [args]\n"
        " -i             - ignore packet CRCs (for compatibility with USB accelerator patch)\n"
        " -p             - packet header output to stderr\n"
        " -P             - full packet dump output to stderr\n"
        " -q             - quiet transfers - no progress updates\n"
        " -v             - verbose output to stderr\n"
        " -d <device>    - USB device, for example /dev/bus/usb/001/003\n"
        " -c <command>   - one of size, dir, get, put, rename, delete, mkdir, reboot, cancel, turbo\n"
        " args           - optional arguments, as required by each command\n\n"
        "Version: " PUPPY_RELEASE ", Compiled: " __DATE__ "\n";
    fprintf(stderr, usageString, myName);
}

int parseArgs(int argc, char *argv[])
{
    extern char *optarg;
    extern int optind;
    int c;

    while((c = getopt(argc, argv, "ipPqvd:c:")) != -1)
    {
        switch (c)
        {
            case 'i':
                ignore_crc = 1;
                break;

            case 'v':
                verbose++;
                break;

            case 'p':
                packet_trace = 1;
                break;

            case 'P':
                packet_trace = 2;
                break;

            case 'q':
                quiet = 1;
                break;

            case 'd':
                devPath = optarg;
                break;

            case 'c':
                if(!strcasecmp(optarg, "dir"))
                    cmd = CMD_HDD_DIR;
                else if(!strcasecmp(optarg, "cancel"))
                    cmd = CANCEL;
                else if(!strcasecmp(optarg, "size"))
                    cmd = CMD_HDD_SIZE;
                else if(!strcasecmp(optarg, "reboot"))
                    cmd = CMD_RESET;
                else if(!strcasecmp(optarg, "put"))
                {
                    cmd = CMD_HDD_FILE_SEND;
                    sendDirection = PUT;
                }
                else if(!strcasecmp(optarg, "get"))
                {
                    cmd = CMD_HDD_FILE_SEND;
                    sendDirection = GET;
                }
                else if(!strcasecmp(optarg, "delete"))
                    cmd = CMD_HDD_DEL;
                else if(!strcasecmp(optarg, "rename"))
                    cmd = CMD_HDD_RENAME;
                else if(!strcasecmp(optarg, "mkdir"))
                    cmd = CMD_HDD_CREATE_DIR;
                else if(!strcasecmp(optarg, "turbo"))
                    cmd = CMD_TURBO;
                break;

            default:
                usage(argv[0]);
                return -1;
        }
    }

    if(cmd == 0)
    {
        usage(argv[0]);
        return -1;
    }

    /* Search for a Toppy if the device is not specified */
    if(devPath == NULL)
    {
        devPath = findToppy();
    }

    if(devPath == NULL)
    {
        return -1;
    }

    if(cmd == CMD_HDD_DIR)
    {
        if(optind < argc)
        {
            arg1 = argv[optind];
        }
        else
        {
            arg1 = "\\";
        }
    }

    if(cmd == CMD_HDD_FILE_SEND)
    {
        if((optind + 1) < argc)
        {
            arg1 = argv[optind];
            arg2 = argv[optind + 1];
        }
        else
        {
            fprintf(stderr,
                    "ERROR: Need both source and destination names.\n");
            return -1;
        }
    }

    if(cmd == CMD_HDD_DEL)
    {
        if(optind < argc)
        {
            arg1 = argv[optind];
        }
        else
        {
            fprintf(stderr,
                    "ERROR: Specify name of file or directory to delete.\n");
            return -1;
        }
    }

    if(cmd == CMD_HDD_RENAME)
    {
        if((optind + 1) < argc)
        {
            arg1 = argv[optind];
            arg2 = argv[optind + 1];
        }
        else
        {
            fprintf(stderr,
                    "ERROR: Specify both source and destination paths for rename.\n");
            return -1;
        }
    }

    if(cmd == CMD_HDD_CREATE_DIR)
    {
        if(optind < argc)
        {
            arg1 = argv[optind];
        }
        else
        {
            fprintf(stderr, "ERROR: Specify name of directory to create.\n");
            return -1;
        }
    }

    if(cmd == CMD_TURBO)
    {
        if(optind < argc)
        {
            arg1 = argv[optind];
        }
        else
        {
            fprintf(stderr, "ERROR: Specify 0=OFF or 1=ON.\n");
            return -1;
        }
    }

    return 0;
}

int isToppy(struct usb_device_descriptor *desc)
{
    return (desc->idVendor == TOPPYVID) && (desc->idProduct == TOPPYPID);
}

/* Read up to valuesize bytes from the file /sys/bus/usb/devices/DEVNAME/ITEM
 * into the string pointed at by value.  Return 1=OK, 0=error */
int readsysfs(char* devname, char* item, char *value, int valuesize)
{
    char filname[SYSPATH_MAX];
    FILE *fil;

    snprintf(filname, SYSPATH_MAX, "/sys/bus/usb/devices/%s/%s", devname, item);
    if (!(fil = fopen(filname, "r"))) return 0;
    fgets(value, valuesize, fil);
    fclose(fil);
    return 1;
}

/* Find a Topfield PVR on the usb.  Return the device as a static string of the
 * form /dev/bus/usb/BBB/DDD or NULL if not found or other error. */
char *findToppy(void)
{
    DIR *devicesdir;
    struct dirent *direntry;
    char vid[5];
    char pid[5];
    char bus[5];
    char device[5];
    static char pathBuffer[32];

    /* Refuse to scan while another instance is running. */
    if(0 != flock(lockFd, LOCK_EX | LOCK_NB))
    {
        fprintf(stderr,
                "ERROR: Can not scan for devices while another instance of puppy is running.\n");
        return NULL;
    }

    pathBuffer[0] = '\0';  /* Signify nothing found at entry */

    /* Iterate over all usb devices, looking for Topfield */
    if (!(devicesdir = opendir("/sys/bus/usb/devices")))
    {
        fprintf(stderr,
                "ERROR: Can not perform autodetection.\n"
                "ERROR: /sys/bus/usb/devices can not be opened.\n"
                "ERROR: %s\n", strerror(errno));
        return NULL;
    }

    while ((direntry = readdir(devicesdir)))
    {
        /* Skip non-device entries */
        if (direntry->d_name[0] == '.'
            || direntry->d_name[0] == 'u'
            || strchr(direntry->d_name, ':')) continue;

        /* Skip if not correct vendor and product ID */
        readsysfs(direntry->d_name, "idVendor", vid, sizeof(vid));
        readsysfs(direntry->d_name, "idProduct", pid, sizeof(pid));
        trace(2, fprintf(stderr, "Found USB device bus-port=%s, vid=%s, pid=%s\n",
                                                direntry->d_name, vid, pid));
        if (strtol(vid, NULL, 16) != TOPPYVID
            || strtol(pid, NULL, 16) != TOPPYPID) continue;

        trace(1, fprintf(stderr, "Recognised Topfield device at bus-port=%s\n",
                                                direntry->d_name));

        /* Error if multiple matching devices found */
        if (pathBuffer[0])
        {
            fprintf(stderr,
                    "ERROR: Multiple Topfield devices recognised.\n"
                    "ERROR: Please use the -d option to specify a device.\n");
            return NULL;
        }

        /* Construct the device path for the first matching device */
        readsysfs(direntry->d_name, "busnum", bus, sizeof(bus));
        readsysfs(direntry->d_name, "devnum", device, sizeof(device));
        sprintf(pathBuffer, "/dev/bus/usb/%03d/%03d", atoi(bus), atoi(device));

        /* Continue iterating, to check for multiple matching devices */
    }
    if (pathBuffer[0])
    {
        return pathBuffer;
    }
    else
    {
        fprintf(stderr, "ERROR: Can not autodetect a Topfield TF5000PVRt\n");
        return NULL;
    }
}
