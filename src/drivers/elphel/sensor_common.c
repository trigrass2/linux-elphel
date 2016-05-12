/** @file sensor_common.h
 * @brief This module handles sensor discovery, initialization and programming tasks
 * common for all sensors. Task that are implemented:
 * - system initialization (?)
 * - compressor write (and global read) pointers
 * - populating 32-byte interframe data
 * - interrupts handling
 * - tasklets
 * - device driver that includes waiting for the next frame regardless of compression
 *
 * Copyright (C) 2016 Elphel, Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//copied from cxi2c.c - TODO:remove unneeded
//#include <linux/module.h>
#include <linux/sched.h>
//#include <linux/slab.h>
//#include <linux/errno.h>
#include <linux/kernel.h>
//#include <linux/fs.h>
//#include <linux/string.h>
#include <linux/init.h>
//#include <linux/autoconf.h>
#include <linux/interrupt.h>
#include <linux/time.h>
//#include <linux/vmalloc.h>
#include <linux/platform_device.h>
//#include <linux/of.h>
//#include <linux/of_device.h>

#include <asm/outercache.h>
#include <asm/cacheflush.h>

//#include <asm/system.h>
//#include <asm/byteorder.h> // endians
//#include <asm/io.h>

//#include <asm/arch/hwregs/intr_vect_defs.h> /// ETRAX interrupt registers

//#include <asm/irq.h>

//#include <asm/delay.h>
//#include <asm/uaccess.h>
#include <elphel/driver_numbers.h>
#include <elphel/c313a.h>
//#include <asm/elphel/fpgaconfa.h>
#include <elphel/exifa.h>
//#include <elphel/x393_types.h>
//#include "fpgactrl.h"  // defines port_csp0_addr, port_csp4_addr
//#include "fpga_sdram.h" // use a single fpga_initSDRAM(void)

//#include "x3x3.h"
//#include "cc3x3.h"
//#include "cxdma.h"

#include "framepars.h"
#include "sensor_common.h"
//#include "pgm_functions.h"
#include "circbuf.h"
//#include "exif353.h"
//#include "histograms.h"
//#include "gamma_tables.h"
#include "quantization_tables.h"
#include "x393_macro.h"
//#include "x393.h"
#include "x393_helpers.h"

/** @brief Driver name to display in log messages. */
#define IMAGEACQ_DRIVER_NAME      "Elphel (R) Model 393 Image Acquisition device driver"
/** @brief The size in bytes of L2 cache invalidation area. This size must be aligned to cache line size.
 * 16 kbytes seems to be good starting point. */
#define L2_INVAL_SIZE             (16 * 1024)

/**@struct jpeg_ptr_t
 * @brief \e jpeg_ptr_t structure contains read and write pointers along with
 * IRQ number for a single channel
 * @var jpeg_ptr_t::jpeg_wr
 * JPEG write pointer in 32 bit words
 * @var jpeg_ptr_t::jpeg_rp
 * JPEG read pointer in 32 bit words
 * @var jpeg_ptr_t::fpga_cntr_prev
 * This field contains previous value of the FPGA transfer counter which is used
 * to find out if it has changed. This pointer is in 32 bit words.
 * @var jpeg_ptr_t::irq_num_comp
 * IRQ number associated with compressor
 * @var jpeg_ptr_t::irq_num_sens
 * IRQ number associated with sensor
 * @var jpeg_ptr_t::chn_num
 * Current channel number
 */
struct jpeg_ptr_t {
	volatile int jpeg_wp;
	volatile int jpeg_rp;
	volatile int fpga_cntr_prev;
	unsigned int irq_num_comp;
	unsigned int irq_num_sens;
	unsigned int chn_num;
	volatile unsigned int flags;
};

/**@struct image_acq_pd_t
 * @brief \e image_acq_pd contains private data for the image acquisition driver
 */
struct image_acq_pd_t {
    int minor;
    struct jpeg_ptr_t jpeg_ptr[IMAGE_CHN_NUM];
};
/* debug code follows */
long long zero_counter[IMAGE_CHN_NUM] = {0};
long long corrected_offset[IMAGE_CHN_NUM] = {0};
long long frame_counter[IMAGE_CHN_NUM] = {0};
long long frame_pos[IMAGE_CHN_NUM][1000] = {0};
long long get_zero_counter(unsigned int chn)
{
	return zero_counter[chn];
}
long long get_corrected_offset(unsigned int chn)
{
	return corrected_offset[chn];
}
long long get_frame_counter(unsigned int chn)
{
	return frame_counter[chn];
}
long long get_frame_pos(unsigned int chn, unsigned int pos)
{
	if (chn < IMAGE_CHN_NUM && pos < 1000)
		return frame_pos[chn][pos];
	return 0;
}
/* end of debug code */

static struct image_acq_pd_t image_acq_priv;

static volatile int JPEG_wp;
static volatile int JPEG_rp;
static int fpga_counter_prev=0; /// Previous value of the FPGA transfer counter (to find out if it did change)
  static struct meta_offsets_t { // works like a cache to time save on looking for tags in the directory (forced to recalcualte if does not match)
    int Image_DateTime;          // will have offset of the Exif_Image_DateTime data in meta page (Exif_Photo_SubSecTime should go immediately after in meta page)
    int Photo_DateTimeOriginal;
    int Photo_ExposureTime;
    int Image_FrameNumber;
    int Image_Orientation;
    int Photo_MakerNote;
  } meta_offsets;

#ifdef TEST_DISABLE_CODE
int  camSeqGetJPEG_wp(void) {return JPEG_wp;}
int  camSeqGetJPEG_rp(void) {return JPEG_rp;}
void camSeqSetJPEG_rp(int p) {
                              JPEG_rp=p;
                              set_globalParam(G_CIRCBUFRP,  p<< 2);
                              set_globalParam(G_FREECIRCBUF,
                                    (((get_globalParam(G_CIRCBUFRP) <= get_globalParam(G_CIRCBUFWP))?
                                       get_globalParam(G_CIRCBUFSIZE):0)+ get_globalParam(G_CIRCBUFRP))
                                       - get_globalParam(G_CIRCBUFWP));
                               }
#endif /* TEST_DISABLE_CODE */

int camseq_get_jpeg_wp(unsigned int chn)
{
	return (chn < IMAGE_CHN_NUM) ? image_acq_priv.jpeg_ptr[chn].jpeg_wp : 0;
}

int camseq_get_jpeg_rp(unsigned int chn)
{
	return (chn < IMAGE_CHN_NUM) ? image_acq_priv.jpeg_ptr[chn].jpeg_rp : 0;
}

void camseq_set_jpeg_rp(unsigned int chn, int ptr)
{
	if (chn < IMAGE_CHN_NUM) {
		image_acq_priv.jpeg_ptr[chn].jpeg_rp = ptr;
	}
}

/*!
   End of compressor-related code - TODO: move to a separate file?
*/

static const struct of_device_id elphel393_sensor_of_match[];
static struct sensorproc_t s_sensorproc; // sensor parameters and functions to call
struct sensorproc_t * sensorproc = NULL;
//EXPORT_SYMBOL_GPL(sensorproc);
//wait_queue_head_t image_acq_wait_queue;  /// queue for the sensor frame interrupts


void tasklet_fpga_function(unsigned long arg);

/**
 * @brief Copy #sensorproc structure, needed for multisensor board to be able
 * to replace some of the functions
 * @param[in]   copy   pointer to a copy structure
 * @return      pointer to a \b copy structure
 */
struct sensorproc_t * copy_sensorproc (struct sensorproc_t * copy)
{
	/** copy sensor functions */
	memcpy(copy, sensorproc, sizeof(struct sensorproc_t));
	return copy;
}

//#ifdef TEST_DISABLE_CODE


///
/// initializes structures for the image acquisition parameter
/// initializes hardware i2c controller and the command sequencer (leaves them stopped to ignore any frame syncs)
/// sets some default acquisition parameters
/// Maybe - set up DMA also?
/// TODO: Take care while turning off reset on i2c and cmd_sequencer so there will be no sensor vsync lost
/// (easier to do in FPGA)
/// Done:
///#define CCAM_VSYNC_ON  port_csp0_addr[X313_WA_DCR1]=X353_DCR1(BLOCKVSYNC,0)
///#define CCAM_VSYNC_OFF port_csp0_addr[X313_WA_DCR1]=X353_DCR1(BLOCKVSYNC,1)
///
int init_acq_sensor(void);

//DECLARE_TASKLET(tasklet_fpga, tasklet_fpga_function, 0); /// 0 - no arguments for now
DECLARE_TASKLET(tasklet_fpga_0, tasklet_fpga_function, 0); /// 0 - no arguments for now
DECLARE_TASKLET(tasklet_fpga_1, tasklet_fpga_function, 1); /// 0 - no arguments for now
DECLARE_TASKLET(tasklet_fpga_2, tasklet_fpga_function, 2); /// 0 - no arguments for now
DECLARE_TASKLET(tasklet_fpga_3, tasklet_fpga_function, 3); /// 0 - no arguments for now
static struct tasklet_struct *tasklets[IMAGE_CHN_NUM] = {&tasklet_fpga_0, &tasklet_fpga_1, &tasklet_fpga_2, &tasklet_fpga_3};


/**
 * @brief Reads FPGA data pointer from the channel given and updates its JPEG_wp
 *
 * This function gets current pointer inside frame buffer and compares it with the previous
 * value. It returns immediately if pointer has not advanced or updates \e jpeg_wr field in #jpeg_ptr_t for
 * current channel. It also tracks the situation when the pointer rolls over.
 * @param[in]   jptr   pointer to #jpeg_ptr_t structure for the channel which data is to be modified
 * @return 0 if compressor was off (no advance) or 1 if write pointer did actually advance
 */
static inline int updateIRQJPEG_wp(struct jpeg_ptr_t *jptr)
{
	phys_addr_t phys_addr;
	void *virt_addr;
//	int prev_dword;
	int xferred;                                           /// number of 32-byte chunks transferred since compressor was reset
	x393_afimux_status_t stat = x393_afimux0_status(jptr->chn_num);

	xferred = stat.offset256 - (jptr->fpga_cntr_prev >> 3);
	if (xferred == 0)
		return 0;                                          /// no advance (compressor was off?)

	jptr->flags |= SENS_FLAG_IRQ;
	jptr->fpga_cntr_prev = jptr->jpeg_wp;
	jptr->jpeg_wp = (stat.offset256 << 3);

	/* debug code follows */
	frame_counter[jptr->chn_num] += 1;
	if (jptr->jpeg_wp == 0) {
		zero_counter[jptr->chn_num] += 1;
		if (zero_counter[jptr->chn_num] < 1000)
			frame_pos[jptr->chn_num][zero_counter[jptr->chn_num] - 1] = frame_counter[jptr->chn_num];
	}
	/* end of debug code */

	/* Correct frame length and re-adjust interframe params.
	 * This operations was scheduled on previous interrupt.
	 */
//	if (jptr->flags & SENS_FLAG_HW_OFF) {
//		int len32;
//		int len32_ptr = jptr->jpeg_wp - INTERFRAME_PARAMS_SZ - 1;
//		phys_addr = circbuf_priv_ptr[jptr->chn_num].phys_addr + DW2BYTE(jptr->jpeg_wp) - CHUNK_SIZE;
//		virt_addr = circbuf_priv_ptr[jptr->chn_num].buf_ptr + jptr->jpeg_wp - INTERFRAME_PARAMS_SZ;
//		outer_inv_range(phys_addr, phys_addr + (CHUNK_SIZE - 1));
//		__cpuc_flush_dcache_area(virt_addr, CHUNK_SIZE);
//		len32 = circbuf_priv_ptr[jptr->chn_num].buf_ptr[len32_ptr];
//		len32 -= INTERFRAME_PARAMS_SZ;
//		circbuf_priv_ptr[jptr->chn_num].buf_ptr[len32_ptr] = len32;
//		__cpuc_flush_dcache_area(virt_addr, CHUNK_SIZE);
//		outer_inv_range(phys_addr, phys_addr + (CHUNK_SIZE - 1));
//		jptr->flags &= ~SENS_FLAG_HW_OFF;
//	}

	/* Looks like compressor is not writing 32 zero bytes when last frame ends precisely at the
	 * end of buffer. Try to detect this situation and set a flag so that we can overwrite first
	 * 32 bytes of the buffer on next interrupt. These bytes will be used as interframe parameters and current frame length
	 * will be decreased by these 32 bytes. Such a measure will corrupt the frame but preserve the structure.
	 */
//	if (jptr->jpeg_wp == 0) {
//		// we need to invalidate two cache lines in order to
//		// estimate the situation correctly: one line after the pointer, which should be the line of
//		// 32 bytes of newly compressed frame(or zero bytes?), and one line before the pointer, which should be the last line of the frame. If this is not done
//		// then the data read from memory can be incorrect and error detection will give false result. Barrier is set to
//		// prevent compiler from reordering operations.
//		phys_addr = circbuf_priv_ptr[jptr->chn_num].phys_addr;
//		virt_addr = circbuf_priv_ptr[jptr->chn_num].buf_ptr;
//		dev_dbg(NULL, "from updateIRQJPEG_wp: phys_addr = 0x%x, virt_addr = 0x%p\n", phys_addr, virt_addr);
//		outer_inv_range(phys_addr, phys_addr + (CHUNK_SIZE - 1));
//		__cpuc_flush_dcache_area(virt_addr, CHUNK_SIZE);
//		phys_addr = circbuf_priv_ptr[jptr->chn_num].phys_addr + CCAM_DMA_SIZE - CHUNK_SIZE;
//		virt_addr = circbuf_priv_ptr[jptr->chn_num].buf_ptr + BYTE2DW(CCAM_DMA_SIZE - CHUNK_SIZE);
//		outer_inv_range(phys_addr, phys_addr + (CHUNK_SIZE - 1));
//		__cpuc_flush_dcache_area(virt_addr, CHUNK_SIZE);
//		dev_dbg(NULL, "from updateIRQJPEG_wp: phys_addr = 0x%x, virt_addr = 0x%p\n", phys_addr, virt_addr);
//		barrier();
//		prev_dword = X393_BUFFSUB(DW2BYTE(jptr->jpeg_wp), 4);
//		dev_dbg(NULL, "circbuf_priv_ptr[jptr->chn_num].buf_ptr[jptr->jpeg_wp] = 0x%x\n", circbuf_priv_ptr[jptr->chn_num].buf_ptr[jptr->jpeg_wp]);
//		dev_dbg(NULL, "circbuf_priv_ptr[jptr->chn_num].buf_ptr[BYTE2DW(prev_dword)] = 0x%x\n", circbuf_priv_ptr[jptr->chn_num].buf_ptr[BYTE2DW(prev_dword)]);
////		if (circbuf_priv_ptr[jptr->chn_num].buf_ptr[jptr->jpeg_wp] == 0x00 &&
//		if ((circbuf_priv_ptr[jptr->chn_num].buf_ptr[BYTE2DW(prev_dword)] & MARKER_FF) == MARKER_FF) {
////			jptr->jpeg_wp += INTERFRAME_PARAMS_SZ;
//			jptr->flags |= SENS_FLAG_HW_OFF;
//			corrected_offset[jptr->chn_num] += 1;
//		}
//	}

	// invalidate CPU L1 and L2 caches
	// the code below was used to find cache coherence issues
	phys_addr = circbuf_priv_ptr[jptr->chn_num].phys_addr + DW2BYTE(jptr->jpeg_wp) - CHUNK_SIZE;
	virt_addr = circbuf_priv_ptr[jptr->chn_num].buf_ptr + jptr->jpeg_wp - INTERFRAME_PARAMS_SZ;
	outer_inv_range(phys_addr, phys_addr + (CHUNK_SIZE - 1));
	__cpuc_flush_dcache_area(virt_addr, CHUNK_SIZE);

	return 1;
}

/**
 * @brief Calculate/update CIRCBUF parameters available after compressor interrupt
 */
inline void update_irq_circbuf(struct jpeg_ptr_t *jptr) {
	/*set_globalParam (G_CIRCBUFWP, JPEG_wp<<2);
     set_globalParam (G_FREECIRCBUF, (((get_globalParam (G_CIRCBUFRP) <= get_globalParam (G_CIRCBUFWP))? get_globalParam (G_CIRCBUFSIZE):0)+ 
                                     get_globalParam (G_CIRCBUFRP)) -    get_globalParam (G_CIRCBUFWP));*/
	/* the concept of global parameters will be changed, use one channel only for testing */
	set_globalParam(G_CIRCBUFWP, jptr->jpeg_wp);
	set_globalParam (G_FREECIRCBUF, (((get_globalParam (G_CIRCBUFRP) <= get_globalParam (G_CIRCBUFWP))? get_globalParam (G_CIRCBUFSIZE):0)+
			get_globalParam (G_CIRCBUFRP)) -    get_globalParam (G_CIRCBUFWP));
}

/**
 * @brief Calculate/update focus parameters available after compressor interrupt
 * NOTE: currently global (latest), not per-frame
 */
inline void updateIRQFocus(struct jpeg_ptr_t *jptr)
{
    //set_globalParam     (G_GFOCUS_VALUE, X313_HIGHFREQ);
    //set_imageParamsThis (P_FOCUS_VALUE, X313_HIGHFREQ);
	u32 high_freq = x393_cmprs_hifreq(jptr->chn_num);
}

inline static void set_default_interframe(struct interframe_params_t *params)
{
	params->height = 1936;
	params->width = 2592;
	params->byrshift = 3;
	params->color = 0;
	params->quality2 = circbuf_quality;
	//params->quality2 = 100;
}

/**
 * @brief Locate area between frames in the circular buffer
 * @return pointer to interframe parameters structure
 */
inline struct interframe_params_t* updateIRQ_interframe(struct jpeg_ptr_t *jptr)
{
	dma_addr_t phys_addr;
	void *virt_addr;
	struct interframe_params_t *interframe = NULL;
	int len_offset = X393_BUFFSUB(DW2BYTE(jptr->jpeg_wp), CHUNK_SIZE + 4);
	int jpeg_len = circbuf_priv_ptr[jptr->chn_num].buf_ptr[BYTE2DW(len_offset)] & FRAME_LENGTH_MASK;
	int jpeg_start = X393_BUFFSUB(DW2BYTE(jptr->jpeg_wp) - CHUNK_SIZE - INSERTED_BYTES(jpeg_len) - CCAM_MMAP_META, jpeg_len);
	// frame_params_offset points to interframe_params_t area before current frame (this parameters belong to the frame below in memory, not the previous)
	int frame_params_offset = BYTE2DW(X393_BUFFSUB(jpeg_start, CHUNK_SIZE));
	int prev_len32_off = X393_BUFFSUB(jpeg_start, CHUNK_SIZE + 4);
	int prev_len32 = circbuf_priv_ptr[jptr->chn_num].buf_ptr[BYTE2DW(prev_len32_off)];

	if ((prev_len32 & MARKER_FF) != MARKER_FF) {
		// try to correct offset
		prev_len32_off = X393_BUFFSUB(prev_len32_off, 0x20);
		prev_len32 = circbuf_priv_ptr[jptr->chn_num].buf_ptr[BYTE2DW(prev_len32_off)];
		if ((prev_len32 & MARKER_FF) == MARKER_FF) {
			frame_params_offset = BYTE2DW(X393_BUFFADD(prev_len32_off, 4));
		}
	}

	interframe = (struct interframe_params_t *) &circbuf_priv_ptr[jptr->chn_num].buf_ptr[frame_params_offset];
	interframe->frame_length = jpeg_len;
	interframe->signffff = 0xffff;

	/* debug code follows */
	set_default_interframe(interframe);
	/* end of debug code */

//	set_globalParam(G_FRAME_SIZE, jpeg_len);

	// invalidate CPU L1 and L2 caches (in this order)
	phys_addr = circbuf_priv_ptr[jptr->chn_num].phys_addr + DW2BYTE(frame_params_offset);
	virt_addr = interframe;
	__cpuc_flush_dcache_area(virt_addr, CHUNK_SIZE);
	outer_inv_range(phys_addr, phys_addr + (CHUNK_SIZE - 1));

   return interframe;
}

/**
 * @brief Fill exif data with the current frame data, save pointer to Exif page in the interframe area
 * @param interframe pointer to interframe parameters structure
 */
inline void updateIRQ_Exif(struct interframe_params_t* interframe) {
   int  index_time = JPEG_wp-11; if (index_time<0) index_time+=get_globalParam (G_CIRCBUFSIZE)>>2;
#ifdef TES_DISABLE_CODE
/// calculates datetime([20] and subsec[7], returns  pointer to char[27]
   char * exif_meta_time_string=encode_time(ccam_dma_buf_ptr[index_time], ccam_dma_buf_ptr[index_time+1]);
/// may be split in datetime/subsec - now it will not notice missing subseq field in template
   write_meta_irq(exif_meta_time_string, &meta_offsets.Photo_DateTimeOriginal,  Exif_Photo_DateTimeOriginal, 27);
   write_meta_irq(exif_meta_time_string, &meta_offsets.Image_DateTime,  Exif_Image_DateTime, 20); // may use 27 if room is provided
   putlong_meta_irq(get_imageParamsThis(P_EXPOS), &meta_offsets.Photo_ExposureTime,  Exif_Photo_ExposureTime);
   putlong_meta_irq(get_imageParamsThis(P_FRAME), &meta_offsets.Image_FrameNumber,   Exif_Image_FrameNumber);
//Exif_Photo_MakerNote
  int global_flips=(get_imageParamsThis(P_FLIPH) & 1) | ((get_imageParamsThis(P_FLIPV)<<1)  & 2);
  int extra_flips=0;
  if (get_imageParamsThis(P_MULTI_MODE)!=0) {
    extra_flips=get_imageParamsThis(P_MULTI_MODE_FLIPS);
    global_flips=extra_flips & 3;
  }
/*  unsigned char orientations[]={1,6,3,8,
                                2,7,4,5,
                                4,5,2,7,
                                3,8,1,6};
*/
  unsigned char orientations[]="1638274545273816";


  unsigned char orientation_short[2];
  orientation_short[0]=0;
  orientation_short[1]=0xf & orientations[(get_imageParamsThis(P_PORTRAIT)&3) | (global_flips<<2)];
  write_meta_irq(orientation_short, &meta_offsets.Image_Orientation,   Exif_Image_Orientation, 2);

//TODO - use memcpy
   int maker_offset;
   maker_offset=putlong_meta_irq(get_imageParamsThis(P_GAINR),   &meta_offsets.Photo_MakerNote, Exif_Photo_MakerNote);
   if (maker_offset>0) {
     putlong_meta_raw_irq(get_imageParamsThis(P_GAING),   maker_offset+4);
     putlong_meta_raw_irq(get_imageParamsThis(P_GAINGB),  maker_offset+8);
     putlong_meta_raw_irq(get_imageParamsThis(P_GAINB),   maker_offset+12);
     putlong_meta_raw_irq(get_imageParamsThis(P_GTAB_R),  maker_offset+16);
     putlong_meta_raw_irq(get_imageParamsThis(P_GTAB_G),  maker_offset+20);
     putlong_meta_raw_irq(get_imageParamsThis(P_GTAB_GB), maker_offset+24);
     putlong_meta_raw_irq(get_imageParamsThis(P_GTAB_B),  maker_offset+28);
     putlong_meta_raw_irq(get_imageParamsThis(P_WOI_LEFT) | (get_imageParamsThis(P_WOI_WIDTH)<<16),  maker_offset+32);
     putlong_meta_raw_irq(get_imageParamsThis(P_WOI_TOP) | (get_imageParamsThis(P_WOI_HEIGHT)<<16),  maker_offset+36);
     putlong_meta_raw_irq(  global_flips |
                          ((get_imageParamsThis(P_BAYER)<<2)     & 0xc) |
                          ((get_imageParamsThis(P_COLOR)<<4)     & 0xF0) |
                          ((get_imageParamsThis(P_DCM_HOR)<<8)   & 0xF00) |
                          ((get_imageParamsThis(P_DCM_VERT)<<12) & 0xF000) |
                          ((get_imageParamsThis(P_BIN_HOR)<<16)  & 0xF0000) |
                          ((get_imageParamsThis(P_BIN_VERT)<<20) & 0xF00000) |
                           (extra_flips <<24) ,  maker_offset+40);
     putlong_meta_raw_irq(get_imageParamsThis(P_MULTI_HEIGHT_BLANK1),   maker_offset+44);
     putlong_meta_raw_irq(get_imageParamsThis(P_MULTI_HEIGHT_BLANK2),   maker_offset+48);
//     putlong_meta_raw_irq(0x1234567,  maker_offset+52); // just testing
     putlong_meta_raw_irq(get_imageParamsThis(P_QUALITY) | ((get_imageParamsThis(P_PORTRAIT)&1)<<7) | (get_imageParamsThis(P_CORING_INDEX)<<16),  maker_offset+52);
     putlong_meta_raw_irq(get_globalParam(G_TEMPERATURE01),  maker_offset+56); // data should be provided by a running daemon
     putlong_meta_raw_irq(get_globalParam(G_TEMPERATURE23),  maker_offset+60);
//get_globalParam(G_TASKLET_CTL)
// left 1 long spare (+44)                          
   }
   interframe->meta_index=store_meta();

#endif /* TES_DISABLE_CODE */
}


/**
 * @brief hardware IRQ service
 * most urgent tasks
 * @param irq 
 * @param dev_id 
 * @return 
 */
/*static irqreturn_t elphel_FPGA_interrupt(int irq, void *dev_id) {
   unsigned long irq_state;

   irq_state = X313_IRQSTATE; //!making dummy read - see c353.h
   DIS_INTERRUPTS;
   PROFILE_NEXT(0);
/// read hardware write pointer (will only advance if compression was on)
///find out if compressor was running and update pointers, exif, ...?
   if (updateIRQJPEG_wp()) { ///also fills P_FRAME ahead
      updateIRQCircbuf();
      updateIRQFocus(); ///NOTE: currently global (latest), not per-frame
      struct interframe_params_t* interframe= updateIRQ_interframe(); /// fills jpeg_len, signffff
/// should we use memcpy as before here?
//      interframe->frame_length=jpeg_len;
//      interframe->signffff=0xffff;
      updateIRQ_Exif(interframe);
      updateFramePars(X3X3_I2C_FRAME, interframe);
      wake_up_interruptible(&circbuf_wait_queue); /// only when frame is acquired
   } else {
      updateFramePars(X3X3_I2C_FRAME, NULL); 
   }
   PROFILE_NOW(1);
   wake_up_interruptible(&framepars_wait_queue); /// all interrupts, not just frames acquired
   tasklet_schedule(&tasklet_fpga); /// trigger software interrupt

   EN_INTERRUPT(SMART);

   return IRQ_HANDLED;
}*/

/**
 * @brief Handle interrupts from sensor channels. This handler is installed without SA_INTERRUPT
 * flag meaning that interrupts are enabled during processing. Such behavior is recommended in LDD3.
 * @param[in]   irq   interrupt number
 * @param[in]   dev_id pointer to driver's private data structure #jpeg_ptr_t corresponding to
 * the channel which raise interrupt
 * @return \e IRQ_HANDLED if interrupt was processed and \e IRQ_NONE otherwise
 */
static irqreturn_t frame_sync_irq_handler(int irq, void *dev_id)
{
	struct jpeg_ptr_t *jptr = dev_id;

	update_frame_pars();
	wake_up_interruptible(&framepars_wait_queue);
//	tasklet_schedule(&tasklet_fpga);
	tasklet_schedule(tasklets[jptr->chn_num]);

	return IRQ_HANDLED;
}

/**
 * @brief Handle interrupts from JPEG compressor channels. This handler is installed without SA_INTERRUPT
 * flag meaning that interrupts are enabled during processing. Such behavior is recommended in LDD3.
 * @param[in]   irq   interrupt number
 * @param[in]   dev_id pointer to driver's private data structure #jpeg_ptr_t corresponding to
 * the channel which raise interrupt
 * @return \e IRQ_HANDLED if interrupt was processed and \e IRQ_NONE otherwise
 */
static irqreturn_t compressor_irq_handler(int irq, void *dev_id)
{
	struct jpeg_ptr_t *jptr = dev_id;
	struct interframe_params_t *interframe = NULL;
	x393_cmprs_interrupts_t irq_ctrl;
	unsigned long flag;

	local_irq_save(flag);
	if (updateIRQJPEG_wp(jptr)) {
//		update_irq_circbuf(priv);
//		updateIRQFocus(priv);
		interframe = updateIRQ_interframe(jptr);
		//updateIRQ_Exif(interframe);
		wake_up_interruptible(&circbuf_wait_queue);
	}
	//wake_up_interruptible(&framepars_wait_queue);

//	tasklet_schedule(&tasklet_fpga);
	tasklet_schedule(tasklets[jptr->chn_num]);
	irq_ctrl.interrupt_cmd = IRQ_CLEAR;
	x393_cmprs_interrupts(irq_ctrl, jptr->chn_num);
	local_irq_restore(flag);

	return IRQ_HANDLED;
}

/**
 * @brief Tasklet - software interrupt
 * lower priority tasks
 * try to implement some balancing - if job is not finished - reduce FPS for it (alternate jobs)?
 * @param arg not used
 */

/*!TODO:

implement 2 modes of controlling when to calculate histograms:
1 - add mask to 3 frame number LSB (i.e. - 0/10000000/10001000/10101010/11111111) - 3 contol bits - en/dis and mode
2 - requested in advance (i.e. by autoexposure when writing new exposure or white balance - when writing balance

mode 1 will provide easy way to display histograms (no need to repetively request them),
mode 2 - useful for autoexposure

Modify waiting (LSEEK_*) for histogrames so they will only unfreeze if the histogram is available (skipping multiple frames))
For displaying histograms - try use latest available - not waiting fro a particular frame.


*/

/// HISTOGRAMS_WAKEUP_ALWAYS if 0 will only wakeup processes waiting for histograms when they become available, maybe never if they are disabled
/// if defined 1 - will wakeup each frame, regardless of the availability of the histograms
//#define HISTOGRAMS_WAKEUP_ALWAYS 0
void tasklet_fpga_function(unsigned long arg) {
  int hist_en;
  int tasklet_disable=get_globalParam(G_TASKLET_CTL);
  unsigned long thisFrameNumber=getThisFrameNumber();
  unsigned long prevFrameNumber=thisFrameNumber-1;
  unsigned long * hash32p=&(framepars[(thisFrameNumber-1) & PARS_FRAMES_MASK].pars[P_GTAB_R]);
  unsigned long * framep= &(framepars[(thisFrameNumber-1) & PARS_FRAMES_MASK].pars[P_FRAME]);
  const struct jpeg_ptr_t *jptr = &image_acq_priv.jpeg_ptr[arg];
  dma_addr_t phys_addr_start, phys_addr_end;
  unsigned int sz;

  /* invalidate L2 cache lines in the beginning of current frame */
	phys_addr_start = circbuf_priv_ptr[jptr->chn_num].phys_addr + DW2BYTE(jptr->fpga_cntr_prev);
	sz = DW2BYTE(jptr->fpga_cntr_prev) + L2_INVAL_SIZE;
	if (sz < CCAM_DMA_SIZE) {
		phys_addr_end = phys_addr_start + L2_INVAL_SIZE - 1;
		outer_inv_range(phys_addr_start, phys_addr_end);
	} else {
		phys_addr_end = phys_addr_start + (CCAM_DMA_SIZE - DW2BYTE(jptr->fpga_cntr_prev) - 1);
		outer_inv_range(phys_addr_start, phys_addr_end);

		phys_addr_start = circbuf_priv_ptr[jptr->chn_num].phys_addr;
		phys_addr_end = phys_addr_start + (sz - CCAM_DMA_SIZE - 1);
		outer_inv_range(phys_addr_start, phys_addr_end);
	}


#ifdef TEST_DISABLE_CODE
/// Time is out?
  if ((getThisFrameNumber() ^ X3X3_I2C_FRAME)  & PARS_FRAMES_MASK) return; /// already next frame
#endif /* TEST_DISABLE_CODE */
/// Histograms are available for the previous frame (that is already in circbuf if compressor was running)
/// Is Y histogram needed?
  PROFILE_NOW(2);
  switch ((tasklet_disable >> TASKLET_CTL_HISTY_BIT) & 7) {
     case TASKLET_HIST_NEVER:   /// never calculate
      hist_en=0;
      break;
     case TASKLET_HIST_HALF:    /// calculate each even (0,2,4,6 frme of 8)
      hist_en= ((thisFrameNumber & 1) ==0) || (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_Y));
      break;
     case TASKLET_HIST_QUATER:  /// calculate twice per 8 (0, 4)
      hist_en= ((thisFrameNumber & 3) ==0) || (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_Y));
      break;
     case TASKLET_HIST_ONCE:    /// calculate once  per 8 (0)
      hist_en= ((thisFrameNumber & 7) ==0) || (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_Y));
      break;
     case TASKLET_HIST_RQONLY:  /// calculate only when specifically requested
      hist_en= (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_Y));
      break;
     case TASKLET_HIST_ALL:     /// calculate each frame
     default:                   /// calculate always (safer)
       hist_en=1;
  }
#ifdef TEST_DISABLE_CODE
  if (hist_en) {
/// after updateFramePars gammaHash are from framepars[this-1]
    set_histograms (prevFrameNumber, (1 << COLOR_Y_NUMBER), hash32p, framep); /// 0x2 Green1
    GLOBALPARS(G_HIST_Y_FRAME)=prevFrameNumber;           /// histogram corresponds to previous frame
    PROFILE_NOW(3);
/// Time is out?
    if ((getThisFrameNumber() ^ X3X3_I2C_FRAME)  & PARS_FRAMES_MASK) return; /// already next frame
#if HISTOGRAMS_WAKEUP_ALWAYS
  }
  wake_up_interruptible(&hist_y_wait_queue);    /// wait queue for the G1 histogram (used as Y)
#else
    wake_up_interruptible(&hist_y_wait_queue);    /// wait queue for the G1 histogram (used as Y)
  }
#endif
#endif /* TEST_DISABLE_CODE */
/// Process parameters 
  if ((tasklet_disable & (1 << TASKLET_CTL_PGM))   == 0) {
      processPars (sensorproc, getThisFrameNumber(), get_globalParam(G_MAXAHEAD)); /// program parameters
      PROFILE_NOW(4);
  }
#ifdef TEST_DISABLE_CODE
/// Time is out?
  if ((getThisFrameNumber() ^ X3X3_I2C_FRAME)  & PARS_FRAMES_MASK) return; /// already next frame
/// Are C histograms needed?
#endif /* TEST_DISABLE_CODE */
  switch ((tasklet_disable >> TASKLET_CTL_HISTC_BIT) & 7) {
     case TASKLET_HIST_NEVER:   /// never calculate
       hist_en=0;
      break;
     case TASKLET_HIST_HALF:    /// calculate each even (0,2,4,6 frme of 8)
      hist_en= ((thisFrameNumber & 1) ==0) || (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_C));
      break;
     case TASKLET_HIST_QUATER:  /// calculate twice per 8 (0, 4)
      hist_en= ((thisFrameNumber & 3) ==0) || (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_C));
      break;
     case TASKLET_HIST_ONCE:    /// calculate once  per 8 (0)
      hist_en= ((thisFrameNumber & 7) ==0) || (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_C));
      break;
     case TASKLET_HIST_RQONLY:  /// calculate only when specifically requested
      hist_en= (get_imageParamsPrev(P_HISTRQ) & (1<<HISTRQ_BIT_C));
      break;
     case TASKLET_HIST_ALL:     /// calculate each frame
     default:                   /// calculate always (safer)
       hist_en=1;
  }
/*
GLOBALPARS(0x1040)=((thisFrameNumber & 1) ==0);
GLOBALPARS(0x1041)=((thisFrameNumber & 3) ==0);
GLOBALPARS(0x1042)=((thisFrameNumber & 7) ==0);
GLOBALPARS(0x1043)=hist_en;
GLOBALPARS(0x1044)=thisFrameNumber;
*/
#ifdef TEST_DISABLE_CODE
  if (hist_en) {
/// after updateFramePars gammaHash are from framepars[this-1]
    set_histograms (prevFrameNumber, 0xf, hash32p, framep); /// all 4 colors, including Y (it will be skipped)
    GLOBALPARS(G_HIST_C_FRAME)=prevFrameNumber;           /// histogram corresponds to previous frame
    PROFILE_NOW(5);
/// Time is out?
    if ((getThisFrameNumber() ^ X3X3_I2C_FRAME)  & PARS_FRAMES_MASK) return; /// already next frame
#if HISTOGRAMS_WAKEUP_ALWAYS
  }
  wake_up_interruptible(&hist_c_wait_queue);     /// wait queue for all the other (R,G2,B) histograms (color)
#else
    wake_up_interruptible(&hist_c_wait_queue);   /// wait queue for all the other (R,G2,B) histograms (color)
  }
#endif
#endif /* TEST_DISABLE_CODE */
}

//#endif /* TEST_DISABLE_CODE */

/**
 * @brief resets compressor and buffer pointers
 */
void reset_compressor(unsigned int chn)
{
	unsigned long flags;

	local_irq_save(flags);
#ifdef TEST_DISABLE_CODE
	port_csp0_addr[X313_WA_COMP_CMD]= COMPCMD_RESET; /// bypasses command sequencer
	if (framepars) set_imageParamsR_all( P_COMPRESSOR_RUN, COMPRESSOR_RUN_STOP );
	else printk ("framepars is not initialized\n");
	/// TODO: There still is a possibility, that there are compressor commands in the hardware que. Should we stop the hardware sequencer here (and restart it later)?
#endif /* TEST_DISABLE_CODE */
	image_acq_priv.jpeg_ptr[chn].jpeg_wp = 0;
	image_acq_priv.jpeg_ptr[chn].jpeg_rp = 0;
	image_acq_priv.jpeg_ptr[chn].fpga_cntr_prev = 0;
	image_acq_priv.jpeg_ptr[chn].flags = 0;
	//update_irq_circbuf(jptr);
	local_irq_restore(flags);
}

/**
 * @brief Camera interrupts on/off  (currently both in the FPGA and in the CPU)
 * @param on 1 - enable, 0 - disable interrupts
 */
void camera_interrupts (int on) {
	int i;
	x393_cmprs_interrupts_t irq_ctrl;

	//MDF2(printk ("camera_interrupts(%d)\n",on));
	dev_dbg(NULL, "set camera interrupts status: %d\n", on);
#ifdef TEST_DISABLE_CODE
	if (on) {
		EN_INTERRUPT(SMART);
	}  else {
		DIS_INTERRUPTS;
	}
	/// clear smart interrupt circuitry in any case
	port_csp0_addr[X313_WA_SMART_IRQ]=0x8000;

	reg_intr_vect_rw_mask intr_mask;
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.ext = on ? 1 : 0;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);
#endif /* TEST_DISABLE_CODE */

	irq_ctrl.interrupt_cmd = on ? IRQ_ENABLE : IRQ_DISABLE;
	for (i = 0; i < IMAGE_CHN_NUM; i++) {
		x393_cmprs_interrupts(irq_ctrl, i);
	}
}

/**
 * @brief sensor_common driver probing function
 * @param[in]   pdev   pointer to \b platform_device structure
 * @return      0 on success or negative error code otherwise
 */
//static int image_acq_init(struct platform_device *pdev)
int image_acq_init(struct platform_device *pdev)
{
	int i;
	int res;
	unsigned int irq;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	const char *frame_sync_irq_names[4] = {"frame_sync_irq_0", "frame_sync_irq_1",
			"frame_sync_irq_2", "frame_sync_irq_3"};
	const char *compressor_irq_names[4] = {"compr_irq_0", "compr_irq_1",
			"compr_irq_2", "compr_irq_3"};

	/* sanity check */
	/*match = of_match_device(elphel393_sensor_of_match, dev);
	if (!match)
		return -EINVAL;*/

	sensorproc= &s_sensorproc;
	//MDD1(printk("sensorproc=0x%x\n",(int) sensorproc));
	dev_dbg(dev, "sensorproc address: 0x%x\n", (int)sensorproc);

	for (i = 0; i < IMAGE_CHN_NUM; i++) {
		irq = platform_get_irq_byname(pdev, frame_sync_irq_names[i]);
		if (request_irq(irq,
				frame_sync_irq_handler,
				0, // no flags
				frame_sync_irq_names[i],
				&image_acq_priv.jpeg_ptr[i])) {
			dev_err(dev, "can not allocate Elphel FPGA interrupts\n");
			return -EBUSY;
		}

		irq = platform_get_irq_byname(pdev, compressor_irq_names[i]);
		if (request_irq(irq,
				compressor_irq_handler,
				0, // no flags
				compressor_irq_names[i],
				&image_acq_priv.jpeg_ptr[i])) {
			dev_err(dev, "can not allocate Elphel FPGA interrupts\n");
			return -EBUSY;
		}
		image_acq_priv.jpeg_ptr[i].irq_num_sens = irq;
		image_acq_priv.jpeg_ptr[i].irq_num_comp = irq;
		image_acq_priv.jpeg_ptr[i].chn_num = i;
	}

	if (init_mmio_ptr() < 0) {
		dev_err(dev, "unable to remap FPGA registers to memory region\n");
		return -EINVAL;
	}


#ifdef TEST_DISABLE_CODE

	if(request_irq(EXT_INTR_VECT,
			elphel_FPGA_interrupt,
			SA_INTERRUPT, // SA_SHIRQ | SA_INTERRUPT if it is a shared one.
			"Elphel FPGA interrupts",
			NULL)) {
		printk(KERN_ERR "Can't allocate Elphel FPGA interrupts");
		return -EBUSY;
	}

#endif

	dev_dbg(dev, "Elphel FPGA interrupts initialized\n");
	dev_dbg(dev, "reset all compressors\n");
	for (i = 0; i < IMAGE_CHN_NUM; i++) {
		reset_compressor(i);
	}
	//reset_compressor(); /// reset compressor and buffer pointers
	//MDD1(printk("x313_dma_init()\n"));
	//x313_dma_init();    /// initialize ETRAX FS DMA
	//MDD1(printk("init_pgm_proc ()\n"));
	//init_pgm_proc ();   /// setup pointers to functions (not sensor-specific)
	//MDD1(printk("reset_qtables()\n"));

	framepars_init(pdev);

	return 0;
}

int image_acq_stop(struct platform_device *pdev)
{
	return 0;
}

//static const struct of_device_id elphel393_sensor_of_match[] = {
//		{ .compatible = "elphel,elphel393-sensor-1.00" },
//		{ /* end of list */ }
//};
//MODULE_DEVICE_TABLE(of, elphel393_sensor_of_match);


/*static struct platform_driver elphel393_sensor_common = {
		.probe          = image_acq_init,
		.remove         = image_acq_stop,
		.driver = {
				.name = IMAGEACQ_DRIVER_NAME,
				.of_match_table = elphel393_sensor_of_match,
		}
};*/

//module_platform_driver(elphel393_sensor_common);

//MODULE_LICENSE("GPL");
//MODULE_AUTHOR("Andrey Filippov <andrey@elphel.com>.");
//MODULE_DESCRIPTION(IMAGEACQ_DRIVER_NAME);
