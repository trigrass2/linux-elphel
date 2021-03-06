/// driver_numbers.h
/// see packages/devices/elphel/Makefile - major numbers should match
/*
#define LLL1 ("one", 2, 3, 4)
#define lll_first(n, ...) n
#define lll_second(x,n, ...) n
const char * ccc1 = lll_first LLL1;
const int iii2 = lll_second LLL1;
#define xxx lll_first LLL1
const char *ccc2 = xxx;
*/
//#define CMOSCAM_MAJOR                 126
#define X3X3_EXIF_MAJOR                 125 // DONE!
#define ELPHEL_MAJOR                    126 // NOT Used
#define STREAM_MAJOR                    127 // Not used (was in sensor_common.c, but no operations)
#define FPGA_MAJOR                      129 // Not used in 393 - was for direct FP:GA access (register, tables, sdram) (/fpgaio, /fsdram, /fpga_tables)
//  @$(MKNOD) -m 0666           $(DEV)/autoexp            c 131   1 - was already rotten
#define FPGA_JTAG_MAJOR                 132
#define FPGA_CLOCK_MAJOR                133
#define X3X3_I2C_MAJOR                  134
#define CIRCBUF_MAJOR                   135
//#define FRAMEPARS_MAJOR  136
#define FRAMEPARS_MAJOR                 130
#define GAMMAS_MAJOR                    137
#define HISTOGRAMS_MAJOR                138
//#define IMAGERAW_MAJOR   139
#define IMAGERAW_MAJOR                  131
#define IMAGEACQ_MAJOR                  140
#define LOGGER_MAJOR                    141
#define VIDEOMEM_MAJOR                  142 // implement raw access to memory and/or 16-bit image buffers over membridge interface
#define DETECT_SENSORS_MAJOR            143 // Maybe not needed?


/// MINORS
#define LOGGER_MINOR                      1
#define LOGGER_CTL_MINOR                  2
#define IMAGERAW_MINOR_FRAME	          1
#define IMAGERAW_MINOR_FPN	              2
#define IMAGERAW_MINOR_UNLOCK       	  3

#define CIRCBUF_MINOR                  0x20
#define CIRCBUF_MINOR_CHN_0            0x20
#define CIRCBUF_MINOR_CHN_1            0x21
#define CIRCBUF_MINOR_CHN_2            0x22
#define CIRCBUF_MINOR_CHN_3            0x23

#define JPEGHEAD_MINOR                 0x30
#define JPEGHEAD_MINOR_CHN_0           0x30
#define JPEGHEAD_MINOR_CHN_1           0x31
#define JPEGHEAD_MINOR_CHN_2           0x32
#define JPEGHEAD_MINOR_CHN_3           0x33

#define HUFFMAN_MINOR                  0x40
#define HUFFMAN_MINOR_CHN_0            0x40
#define HUFFMAN_MINOR_CHN_1            0x41
#define HUFFMAN_MINOR_CHN_2            0x42
#define HUFFMAN_MINOR_CHN_3            0x43

#define CMOSCAM_MINOR_FRAMEPARS_CHN_0  0x50
#define CMOSCAM_MINOR_FRAMEPARS_CHN_1  0x51
#define CMOSCAM_MINOR_FRAMEPARS_CHN_2  0x52
#define CMOSCAM_MINOR_FRAMEPARS_CHN_3  0x53

#define CMOSCAM_MINOR_RWTABLES            9
#define CMOSCAM_MINOR_CIRCBUF            11
#define CMOSCAM_MINOR_HISTOGRAM          12
#define CMOSCAM_MINOR_JPEAGHEAD          13
#define CMOSCAM_MINOR_GAMMA              14
#define CMOSCAM_MINOR_FRAMEPARS          16
#define CMOSCAM_MINOR_GAMMAS             17
#define CMOSCAM_MINOR_HISTOGRAMS         18
#define CMOSCAM_MINOR_IMAGEACQ           19
#define CMOSCAM_MINOR_HUFFMAN            20

#define FPGACONF_MINOR_IORW	              3   ///< direct R/W FPGA registers
#define FPGACONF_MINOR_SDRAM              4   ///< read/write SDRAM through PIO
#define FPGACONF_MINOR_TABLES             6   ///< Write FPGA tables directly
 

#define FPGA_CLOCK_MINOR                  2
#define FPGA_CLOCK_MINOR_I2C              2
#define FPGA_CLOCK_MINOR_CLOCKS           3


#define FPGA_JTAG_RESET_MINOR             0  // just close open files
#define FPGA_JTAG_RAW_MINOR               0  // just close open files
#define FPGA_JTAG_MINOR                   1
#define FPGA_SJTAG_MINOR                  2
#define FPGA_AJTAG_MINOR                  3
#define FPGA_JTAG_BOUNDARY_MINOR          5  // read/write boundary pins of the main FPGA
#define FPGA_SJTAG_BOUNDARY_MINOR         6  // read/write boundary pins of the sensor board FPGA
#define FPGA_AJTAG_BOUNDARY_MINOR         7  // read/write boundary pins of the aux board FPGA

#define FPGA_SJTAG_CHANNELS               4 // Number of sensor ports for JTAG
#define FPGA_SJTAG_MINOR_OFFSET           8 // Minors range start for the sensor port JTAG
#define FPGA_SJTAG_BOUNDARY_OFFSET       12 // Minors range start for the sensor port boundary

//#define X3X3_EXIF_EXIF                  0 // read encoded Exif data (SEEK_END,
//#define X3X3_EXIF_META                  1 // write metadata, concurently opened files. All writes are atomic
// control/setup devices
#define X3X3_EXIF_TEMPLATE                2 // write Exif template
#define X3X3_EXIF_METADIR                 3 // write metadata to Exif header translation (dir_table[MAX_EXIF_FIELDS])
// those 2 files will disable exif_enable and exif_valid, truncate file size to file pointer on release.
#define X3X3_EXIF_TIME                    4 // write today    omorrow date (YYYY:MM:DD) and number of seconds at today    omorrow
                             // midnight (00:00:00) in seconds from epoch (long, startting from LSB)

#define X3X3_EXIF_EXIF_CHN_0           0x10 // read encoded Exif data (SEEK_END,
#define X3X3_EXIF_EXIF_CHN_1           0x11 // read encoded Exif data (SEEK_END,
#define X3X3_EXIF_EXIF_CHN_2           0x12 // read encoded Exif data (SEEK_END,
#define X3X3_EXIF_EXIF_CHN_3           0x13 // read encoded Exif data (SEEK_END,
#define X3X3_EXIF_META_CHN_0           0x20 // write metadata, concurently opened files. All writes are atomic
#define X3X3_EXIF_META_CHN_1           0x21 // write metadata, concurently opened files. All writes are atomic
#define X3X3_EXIF_META_CHN_2           0x22 // write metadata, concurently opened files. All writes are atomic
#define X3X3_EXIF_META_CHN_3           0x23 // write metadata, concurently opened files. All writes are atomic

