/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#include <features.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "libbb.h"

/* For 2.6, use the cleaned up header to get the 64 bit API. */
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/loop.h>
typedef struct loop_info64 bb_loop_info;
#define BB_LOOP_SET_STATUS LOOP_SET_STATUS64
#define BB_LOOP_GET_STATUS LOOP_GET_STATUS64

/* For 2.4 and earlier, use the 32 bit API (and don't trust the headers) */
#else
/* Stuff stolen from linux/loop.h for 2.4 and earlier kernels*/
#include <linux/posix_types.h>
#define LO_NAME_SIZE        64
#define LO_KEY_SIZE         32
#define LOOP_SET_FD         0x4C00
#define LOOP_CLR_FD         0x4C01
#define BB_LOOP_SET_STATUS  0x4C02
#define BB_LOOP_GET_STATUS  0x4C03
typedef struct {
	int                lo_number;
	__kernel_dev_t     lo_device;
	unsigned long      lo_inode;
	__kernel_dev_t     lo_rdevice;
	int                lo_offset;
	int                lo_encrypt_type;
	int                lo_encrypt_key_size;
	int                lo_flags;
	char               lo_file_name[LO_NAME_SIZE];
	unsigned char      lo_encrypt_key[LO_KEY_SIZE];
	unsigned long      lo_init[2];
	char               reserved[4];
} bb_loop_info;
#endif

extern int del_loop(const char *device)
{
	int fd,rc=0;

	if ((fd = open(device, O_RDONLY)) < 0) rc=1;
	else {
		if (ioctl(fd, LOOP_CLR_FD, 0) < 0) rc=1;
		close(fd);
	}
	return rc;
}

// Returns 0 if mounted RW, 1 if mounted read-only, <0 for error.
// *device is loop device to use, or if *device==NULL finds a loop device to
// mount it on and sets *device to a strdup of that loop device name.  This
// search will re-use an existing loop device already bound to that
// file/offset if it finds one.
extern int set_loop(char **device, const char *file, int offset)
{
	char dev[20];
	bb_loop_info loopinfo;
	struct stat statbuf;
	int i, dfd, ffd, mode, rc=1;

	// Open the file.  Barf if this doesn't work.
	if((ffd = open(file, mode=O_RDWR))<0 && (ffd = open(file,mode=O_RDONLY))<0)
		return errno;
	
	// Find a loop device
	for(i=0;rc;i++) {
		sprintf(dev, LOOP_FORMAT, i++);
		// Ran out of block devices, return failure.
		if(stat(*device ? : dev, &statbuf) || !S_ISBLK(statbuf.st_mode)) {
			rc=ENOENT;
			break;
		}
		// Open the sucker and check its loopiness.
		if((dfd=open(dev, mode))<0 && errno==EROFS)
			dfd=open(dev,mode=O_RDONLY);
		if(dfd<0) continue;

		rc=ioctl(dfd, BB_LOOP_GET_STATUS, &loopinfo);
		// If device free, claim it.
		if(rc && errno==ENXIO) {
			memset(&loopinfo, 0, sizeof(loopinfo));
			safe_strncpy(loopinfo.lo_file_name, file, LO_NAME_SIZE);
			loopinfo.lo_offset = offset;
			// Associate free loop device with file
			if(!ioctl(dfd, LOOP_SET_FD, ffd) &&
			   !ioctl(dfd, BB_LOOP_SET_STATUS, &loopinfo)) rc=0;
			else ioctl(dfd, LOOP_CLR_FD, 0);
		// If this block device already set up right, re-use it.
		// (Yes this is racy, but associating two loop devices with the same
		// file isn't pretty either.  In general, mounting the same file twice
		// without using losetup manually is problematic.)
		} else if(strcmp(file,loopinfo.lo_file_name)
					|| offset!=loopinfo.lo_offset) rc=1;
		close(dfd);
		if(*device) break;
	}
	close(ffd);
	if(!rc) {
		if(!*device) *device=strdup(dev);
		return mode==O_RDONLY ? 1 : 0;
	} else return rc;
}

/* END CODE */
/*
Local Variables:
c-file-style: "linux"
c-basic-offset: 4
tab-width: 4
End:
*/
