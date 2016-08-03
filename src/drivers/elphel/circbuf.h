/** @file circbuf.h
 *
 * @brief Drivers to manipulate large circular buffer that holds compressed
 * images/video. Buffer frame data is filled in by the FPGA, frame pointers and
 * essential frames metadata filled during servicing of the interrupts.
 *
 * @copyright Copyright (C) 2016 Elphel, Inc
 *
 * @par <b>License</b>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CIRCBUF_H
#define _CIRCBUF_H

#include <linux/poll.h>

/** @brief Circular buffer private data */
struct circbuf_priv_t {
	int                 minor;                             ///< device file minor number
	unsigned long       *buf_ptr;                          ///< pointer to circular buffer memory region
	dma_addr_t          phys_addr;                         ///< physical address of memory region reported by memory driver
};
extern struct circbuf_priv_t *circbuf_priv_ptr;
extern wait_queue_head_t circbuf_wait_queue;

int          circbuf_all_open  (struct inode *inode,  struct file *filp); // set file size
int          circbuf_all_release(struct inode *inode, struct file *filp);
loff_t       circbuf_all_lseek (struct file * file,   loff_t offset, int orig);
ssize_t      circbuf_all_write (struct file * file,   const char * buf, size_t count, loff_t *off);
ssize_t      circbuf_all_read  (struct file * file,   char * buf, size_t count, loff_t *off);
int          circbuf_all_mmap  (struct file *file,    struct vm_area_struct *vma);
unsigned int circbuf_all_poll  (struct file *file,    poll_table *wait);

int          circbuf_open  (struct inode *inode, struct file *filp);      // set file size
loff_t       circbuf_lseek (struct file * file, loff_t offset, int orig);
ssize_t      circbuf_write (struct file * file, const char * buf, size_t count, loff_t *off);
ssize_t      circbuf_read  (struct file * file, char * buf, size_t count, loff_t *off);
int          circbuf_mmap  (struct file *file, struct vm_area_struct *vma);
unsigned int circbuf_poll  (struct file *file,    poll_table *wait);

/* debug code follows */
extern unsigned short circbuf_quality;
extern unsigned short circbuf_height;
extern unsigned short circbuf_width;
extern unsigned char  circbuf_byrshift;
/* end of debug code */

/* this should be placed to drivers common includes */
struct fvec {
	void *iov_base;                             ///< pointer to allocated buffer
	size_t iov_len;                             ///< the size (in bytes) of allocated buffer; set after allocation and is not modified during buffer lifetime
	dma_addr_t iov_dma;                         ///< buffer physical address
};
/* end of common includes */
int circbuf_get_ptr(int sensor_port, size_t offset, size_t len, struct fvec *vect_0, struct fvec *vect_1);

#endif /* _CIRCBUF_H */
