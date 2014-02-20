/*
 * CSR SiRFprima2 VIP hardware registers
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __VIP_REGS_H__
#define __VIP_REGS_H__

#define CAM_COUNT               0x0000
#define CAM_INT_COUNT           0x0004
#define CAM_START               0x0008
#define CAM_END                 0x000C
#define CAM_CTRL                0x0010
#define CAM_PIXEL_SHIFT         0x0014
#define CAM_YUV_COEFR           0x0018
#define CAM_YUV_COEFG           0x001C
#define CAM_YUV_COEFB           0x0020
#define CAM_YUV_OFFSET          0x0024
#define CAM_INT_EN              0x0028
#define CAM_INT_CTRL            0x002C
#define CAM_VSYNC_CTRL          0x0030
#define CAM_HSYNC_CTRL          0x0034
#define CAM_PXCLK_CTRL          0x0038
#define CAM_VSYNC_HSYNC         0x003C
#define CAM_TIMING_CTRL         0x0040
#define CAM_DMA_CTRL            0x0044
#define CAM_DMA_LEN             0x0048
#define CAM_FIFO_CTRL_REG       0x004C
#define CAM_FIFO_LEVEL_CHECK    0x0050
#define CAM_FIFO_OP_REG         0x0054
#define CAM_FIFO_STATUS_REG     0x0058
#define CAM_RD_FIFO_DATA        0x005C
#define CAM_TS_CTRL             0x0060


#define DMA_CH0_ADDR            0x000
#define DMA_CH0_XLEN            0x004
#define DMA_CH0_YLEN            0x008
#define DMA_CH0_CTRL            0x00C
#define DMA_WIDTH_0             0x100
#define DMA_CHN_VALID           0x140
#define DMA_CHN_INT_ENABLE      0x144
#define DMA_INT_ENABLE          0x148
#define DMA_CH_LOOP_CTRL        0x150



/*
 * Register bit field definitions
 */
typedef union
{
    struct
    {
      DWORD XC:     16;     /*The value of the current column of the input pixel from sensor*/
      DWORD YC:     16;     /*The value of the current row of the input pixel from sensor*/
    };
    DWORD DW;
} REG_CAM_COUNT;

typedef union
{
    struct
    {
      DWORD XI:     16;     /*Camera will generate an interrupt when the current XCOUNT value equals to the XINT value*/
      DWORD YI:     16;     /*Camera will generate an interrupt when the current YCOUNT value equals to the YINT value*/
    };
    DWORD DW;
} REG_CAM_INT_COUNT;

typedef union
{
    struct
    {
      DWORD XS:     16;     /*The value of the start column of the active region*/
      DWORD YS:     16;     /*The value of the start row of the active region*/
    };
    DWORD DW;
} REG_CAM_START;

typedef union
{
    struct
    {
      DWORD XE:     16;     /*The value of the end column of the active region*/
      DWORD YE:     16;     /*The value of the end row of the active region*/
    };
    DWORD DW;
} REG_CAM_END;

typedef union
{
    struct
    {
      DWORD PXCLK_CTRL:1;      /*0: PIXCLK generated from an external sensor
                                 1: PIXCLK generated internally
                                */
      DWORD HSYNC_CTRL:1;      /*0: HSYNC generated from an external sensor
                                 1: HSYNC generated internally
                                */
      DWORD VSYNC_CTRL:1;      /*0: VSYNC generated from an external sensor
                                 1: VSYNC generated internally
                                */
      DWORD PIXCLK_INV:1;      /*Inverts the polarity of the input PIXCLK*/
      DWORD HSYNC_INV: 1;      /*Inverts the polarity of the input HSYNC*/
      DWORD VSYNC_INV: 1;      /*Inverts the polarity of the input VSYNC*/
      DWORD SINGLE:    1;      /*1'b1: Captures one frame image
                                 1'b0: Continuous capture
                                */
      DWORD IO_TRIGGER:1;      /*0: PIXCLK directly passes through
                                 1: PIXCLK sampled by IOCLK
                                */

      DWORD YUVYCrCb:  1;      /*1: Input pixel data is in YUV format
                                 0: Input pixel data is in YCrCb format
                                */
      DWORD YUV_FORMAT:3;      /*Raw pixel data input sequence:
                                 000: YUYV/YCrYCb
                                 001: UYYV/CrYYCb
                                 010: YUVY/YCrCbY
                                 011: UYVY/CrYCbY
                                 100: YVYU/YCbYCr
                                 101: VYYU/CbYYCr
                                 110: YVUY/YCbCrY
                                 111: VYUY/CbYCrY
                               */
      DWORD OUT_FORMAT: 2;     /*RGB format sent to the FIFO after YUV to RGB conversion is completed:
                                 00: 8:8:8
                                 01: 6:5:5
                                 10: 5:5:6
                                 11: 5:6:5
                                */
      DWORD YUVRGB:     1;     /*0: Bypasses YUV to RGB conversion
                                 1: Convert data format from YUV to RGB
                                */
      DWORD RESERVE0:   1;
      DWORD X_SCA:      2;     /*Contraction ratio in column direction:
                                 2'b00: no contraction
                                 2'b01: 1:2 contraction
                                 2'b10: 1:4
                                 2'b11: 1:8
                                 This must be 2¡¯b00 when the yuv->rgb module is bypassed
                                */
      DWORD Y_SCA:      2;     /*Contraction ratio in row direction:
                                 2'b00: no contraction
                                 2'b01: 1:2 contraction
                                 2'b10: 1:4
                                 2'b11: 1:8
                                 This must be 2¡¯b00 when the yuv->rgb module is bypassed
                                */
#ifdef CONFIG_ARCH_ATLAS6
      DWORD HOR_MIRROR: 1;     /*0: disable L/R mirror function
                                 1: enable L/R mirror functio
                                */
      DWORD CAP_FROM_ODD: 1;   /*0: VIP start to capture field data freely
                                 1: VIP start to capture field data from the first odd field
                                */
      DWORD CAP_FROM_EVEN: 1;  /*0: VIP start to capture field data freely
                                 1: VIP start to capture field data from the first even field
                                */
	DWORD PAD_MUX_ON_UPLI : 1;/*0: normal
                                 1: pad mux on UPLI
                                */
#else
      DWORD RESERVE2:   4;
#endif
      DWORD CCIR656_EN: 1;     /*CCIR656 enable*/
      DWORD FID:        1;     /*FID input, read only*/
      DWORD RESERVE3:   5;
      DWORD INIT:       1;     /*Reset camera control module, configuration data will still be reserved.
                                 Logic remains in the reset until a 0 is written to this bit
                                */
    };
    DWORD DW;
} REG_CAM_CTRL;

typedef union
{
    struct
    {
      DWORD PIXEL_SHIFT: 3;     /*Pixel bit select options. The input pixel data is always connected to the lowest data pin
                                  3'b000: select pxd_data[15:0] as valid data
                                  3'b001: select pxd_data[7:0] as valid data
                                  3'b010: select pxd_data[8:1] as valid data
                                  3'b011: store pxd_data[9:2] as valid data
                                  3'b100: select pxd_data[10:3] as valid data
                                  3'b101: select pxd_data[11:4] as valid data
                                  3'b110: select pxd_data[14:7] as valid data
                                  3'b111: store pxd_data[15:8] as valid dat
                                 */
      DWORD RESERVE:    29;     /*The value of the current row of the input pixel from sensor*/
    };
    DWORD DW;
} REG_CAM_PIXEL_SHIFT;

typedef union
{
    struct
    {
      DWORD C1:        10;      /*V coefficient for R*/
      DWORD C2:        10;      /*U coefficient for R*/
      DWORD C3:        10;      /*Y coefficient for R*/
      DWORD RESERVE:    2;
    };
    DWORD DW;
} REG_CAM_YUV_COEF1;

typedef union
{
    struct
    {
        DWORD C4:      10;    /*V coefficient for G*/
        DWORD C5:      10;    /*U coefficient for G*/
        DWORD C6:      10;    /*Y coefficient for G*/
        DWORD RESERVE:  2;
    };
    DWORD DW;
} REG_CAM_YUV_COEF2;

typedef union
{
    struct
    {
      DWORD C7:     10;      /*V coefficient for B*/
      DWORD C8:     10;      /*U coefficient for B*/
      DWORD C9:     10;      /*Y coefficient for B*/
      DWORD RESERVE: 2;
    };
    DWORD DW;
} REG_CAM_YUV_COEF3;

typedef union
{
    struct
    {
      DWORD OFFSET1:10;     /*Offset coefficient for R*/
      DWORD OFFSET2:10;     /*Offset coefficient for G*/
      DWORD OFFSET3:10;     /*Offset coefficient for B*/
      DWORD RESERVE: 2;
    };
    DWORD DW;
} REG_CAM_YUV_OFFSET;

typedef union
{
    struct
    {
      DWORD SENSOR_INT_EN:1;    /*Enables sensor interrupt*/
      DWORD FIFO_OFLOW_EN:1;    /*Enables FIFO overflow interrupt*/
      DWORD FIFO_UFLOW_EN:1;    /*Enables FIFO underflow interrupt*/
      DWORD TS_OVER_EN:   1;    /*Enables a TS frame over interrupt*/
      DWORD RESERVE:     28;
    };
    DWORD DW;
} REG_CAM_INT_EN;

typedef union
{
    struct
    {
        DWORD SENSOR_INT:1;     /*When the value of CAM_COUNT equals to that of the CAM_INT_COUNT
                                  register, this interrupt will be generated; write a 1 to this bit
                                  to reset it
                                 */
        DWORD FIFO_OFLOW:1;     /*FIFO is overflown. Write a 1 to this bit to reset it*/
        DWORD FIFO_UFLOW:1;     /*FIFO is underflown. Write a 1 to this bit to reset it*/
        DWORD TS_OVER:   1;     /*TS one Frame over. Write a 1 to this bit to reset it*/
        DWORD RESERVE:  28;
    };
    DWORD DW;
} REG_CAM_INT_CTRL;

typedef union
{
    struct
    {
      DWORD VSYNC_ACT_NUM:  16; /*VSYNC active width in HSYNC number*/
      DWORD VSYNC_BLANK_NUM:16; /*VSYNC blank width in HSYNC number*/
    };
    DWORD DW;
} REG_CAM_VSYNC_CTRL;

typedef union
{
    struct
    {
      DWORD HSYNC_ACT_NUM:  16; /*HSYNC active width in HSYNC number*/
      DWORD HSYNC_BLANK_NUM:16; /*HSYNC blank width in HSYNC number*/
    };
    DWORD DW;
} REG_CAM_HSYNC_CTRL;

typedef union
{
    struct
    {
      DWORD PIXCLK_NUM:16;  /*PIXCLK number in IOCLK number. Actual output period of
                              PIXCLK equals to 2*(PIXCLK_NUM+1)*IOCLK period
                             */
      DWORD RESERVE:   16;
    };
    DWORD DW;
} REG_CAM_PIXCLK_CTRL;

typedef union
{
    struct
    {
        DWORD VSYNC_HSYNC:16; /*Pixel number between the first HSYNC and VSYNC*/
        DWORD VSYNC_WIDTH:16; /*VSYNC width number in the number of PIXCLK period*/
    };
    DWORD DW;
} REG_CAM_VSYNC_HSYNC;

typedef union
{
    struct
    {
      DWORD PCLK_POLAR: 1;  /*Inverts pixel clock:
                              1 = Inverts pixel clock (i.e. pixel clock ? phase off)
                              0 = Do not invert pixel clock
                             */
      DWORD HSYNC_POLAR:1;  /*Inverts the horizontal sync signal:
                              1 = Horizontal sync signal is active low
                              0 = Horizontal sync signal is active high
                             */
      DWORD VSYNC_POLAR:1;  /*Inverts the vertical sync signal:
                              1 = vertical sync signal is active low
                              0 = vertical sync signal is active high
                             */
      DWORD HSYNC_MASK: 1;  /*Masks HSYNC control:
                              1 = Disables the HSYNC during the vertical blank time
                              0 = Enables the HSYNC during the vertical blank time
                             */
      DWORD RESERVE:   28;
    };
    DWORD DW;
} REG_CAM_TIMING_CTRL;

typedef union
{
    struct
    {
      DWORD DMA_IO:      1; /*0: DMA operation
                              1: IO operation
                             */
      DWORD RESERVE1:    1;
      DWORD DMA_FLUSH:   1; /*Flushes the DMA receive FIFO if the data length at the peripheral
                              does not match the DWORD size in the DMA control
                             */
      DWORD RESERVE2:    1;
      DWORD ENDIAN_MODE: 2; /*00: No change
                              01: Byte exchange in DWORD
                              10: Word exchange in DWORD
                              11: Byte exchange in WORD
                             */
      DWORD RESERVE3:   26;
    };
    DWORD DW;
} REG_CAM_DMA_CTRL;

typedef union
{
    struct
    {
        DWORD DATA_LEN:32;/*The byte length of a DMA transfer. If it's set to zero,
                            then DMA transfer will operate continuously until it is stopped
                           */
    };
    DWORD DW;
} REG_CAM_DMA_LEN;

typedef union
{
    struct
    {
      DWORD FIFO_WIDTH:2;   /*00: Byte-mode FIFO
                              01: Word-mode FIFO
                              10: Dword-mode FIFO
                              11: Dword-mode FIFO
                             */
      DWORD RESERVE1: 30;
    };
    DWORD DW;
} REG_CAM_FIFO_CTRL;

typedef union
{
    struct
    {
      DWORD FIFO_SC: 7;  /*FIFO stop check in DWORD length*/
      DWORD RESERVE1:3;
      DWORD FIFO_LC: 7;  /*FIFO low check in DWORD length*/
      DWORD RESERVE2:3;
      DWORD FIFO_HC: 7;  /*FIFO high check in DWORD length*/
      DWORD RESERVE3:5;
    } ;
    DWORD DW;
} REG_CAM_FIFO_LEVEL_CHK;

typedef union
{
    struct
    {
      DWORD FIFO_START:1;   /*Starts the FIFO transfer when this bit is declared*/
      DWORD FIFO_RESET:1;   /*Set to 1 to stop the FIFO and reset the FIFO internal status,
                              including the relevant interrupt status. Set to 0 in normal operation
                             */
      DWORD RESERVE1: 30;
    } ;
    DWORD DW;
} REG_CAM_FIFO_OP;

typedef union
{
    struct
    {
      DWORD FIFO_LEVEL:9;   /*The byte count of the valid data in the FIFO varies from 0 to 511 bytes.
                              In case FIFO is full, the value of this register will be set to 0, thus
                              users must concatenate the FIFO_FULL bit with this value to determine
                              the actual data count in it
                             */
      DWORD FIFO_FULL: 1;   /*FIFO full status; FIFO is full when it¡¯s read out as 1. This bit is
                              concatenated with FIFO_LEVEL to be the actual FIFO data count
                             */
      DWORD FIFO_EMPTY:1;   /*FIFO empty status,
                              equivalent to (FIFO_FULL, FIFO_LEVEL) = 0
                             */
      DWORD RESERVE:  21;
    } ;
    DWORD DW;
} REG_CAM_FIFO_STATUS;


typedef union
{
    struct
    {
        DWORD FIFO_DATA:32; /*FIFO data read by RISC or DSP*/
    } ;
    DWORD DW;
} REG_CAM_FIFO_DATA;


typedef union
{
    struct
    {
      DWORD RESERVE0:  4;
      DWORD VIP_TS:    1;   /*0: VIP is configured to receive CCIR656/CCD/CMOS sensor
                              1: VIP is configured to receive Transport Stream
                             */
      DWORD NEG_SAMPLE:1;   /*0: Using pos-edge of the nput clock to sample psync/dvalid/data
                              1: Using neg-edge of input clock to sample psync/dvalid/data
                             */
      DWORD SINGLE:    1;   /*1'b1: Capture one frame image
                              1'b0: Continuous capture
                             */
      DWORD ENDIAN:    1;   /*1'b1:Big-endian, first input byte is in [31:24]
                              1'b0:Little-endian, first input byte is in [7:0]
                             */
      DWORD RESERVE1: 24;
      DWORD INIT:      1;   /*Reset the TS control module and the configuration data will
                              still be reserved. Logic will remain in the reset until a 0
                              is written to this bit
                             */
    } ;
    DWORD DW;
} REG_CAM_TS_CTRL;


typedef union
{
    struct
    {
        unsigned chn0               : 1;
        unsigned chn1               : 1;
        unsigned chn2               : 1;
        unsigned chn3               : 1;
        unsigned chn4               : 1;
        unsigned chn5               : 1;
        unsigned chn6               : 1;
        unsigned chn7               : 1;
        unsigned chn8               : 1;
        unsigned chn9               : 1;
        unsigned chn10              : 1;
        unsigned chn11              : 1;
        unsigned chn12              : 1;
        unsigned chn13              : 1;
        unsigned chn14              : 1;
        unsigned chn15              : 1;
        unsigned rsvd0              : 16;
    };
    DWORD DW;
}REG_DMA_CHN_VALID;

typedef union
{
    struct
    {
        unsigned chn0               : 1;
        unsigned chn1               : 1;
        unsigned chn2               : 1;
        unsigned chn3               : 1;
        unsigned chn4               : 1;
        unsigned chn5               : 1;
        unsigned chn6               : 1;
        unsigned chn7               : 1;
        unsigned chn8               : 1;
        unsigned chn9               : 1;
        unsigned chn10              : 1;
        unsigned chn11              : 1;
        unsigned chn12              : 1;
        unsigned chn13              : 1;
        unsigned chn14              : 1;
        unsigned chn15              : 1;
        unsigned rsvd0              : 16;
    };
    DWORD DW;
}REG_DMA_INT_ENABLE;

typedef union
{
    struct
    {
        unsigned width:32;
    };
    DWORD DW;
}REG_DMA_WIDTH;

typedef union
{
    struct
    {
        unsigned bufA_valid0        : 1;
        unsigned bufA_valid1        : 1;
        unsigned bufA_valid2        : 1;
        unsigned bufA_valid3        : 1;
        unsigned bufA_valid4        : 1;
        unsigned bufA_valid5        : 1;
        unsigned bufA_valid6        : 1;
        unsigned bufA_valid7        : 1;
        unsigned bufA_valid8        : 1;
        unsigned bufA_valid9        : 1;
        unsigned bufA_valid10       : 1;
        unsigned bufA_valid11       : 1;
        unsigned bufA_valid12       : 1;
        unsigned bufA_valid13       : 1;
        unsigned bufA_valid14       : 1;
        unsigned bufA_valid15       : 1;
        unsigned bufB_valid0        : 1;
        unsigned bufB_valid1        : 1;
        unsigned bufB_valid2        : 1;
        unsigned bufB_valid3        : 1;
        unsigned bufB_valid4        : 1;
        unsigned bufB_valid5        : 1;
        unsigned bufB_valid6        : 1;
        unsigned bufB_valid7        : 1;
        unsigned bufB_valid8        : 1;
        unsigned bufB_valid9        : 1;
        unsigned bufB_valid10       : 1;
        unsigned bufB_valid11       : 1;
        unsigned bufB_valid12       : 1;
        unsigned bufB_valid13       : 1;
        unsigned bufB_valid14       : 1;
        unsigned bufB_valid15       : 1;
    };
    DWORD DW;
}REG_DMA_CHN_LOOP_CTRL;

typedef union
{
    struct
    {
        unsigned addr               : 29;
        unsigned rsvd0              : 3;
    };
    DWORD DW;
}REG_DMA_ADDRESS;

typedef union
{
  struct
  {
    unsigned xl                 : 12;
    unsigned rsvd0              : 20;
  };
  DWORD DW;
}REG_DMA_XLEN;

typedef union
{
    struct
    {
        unsigned yl                 : 12;
        unsigned rsvd0              : 20;
    };
    DWORD DW;
}REG_DMA_YLEN;

typedef union
{
    struct
    {
        unsigned ws                 : 4;
        unsigned burst              : 1;
        unsigned dir                : 1;
        unsigned rsvd0              : 26;
    };
    DWORD DW;
}REG_DMA_CTRL;


#endif  // __VIP_REGS_H__
