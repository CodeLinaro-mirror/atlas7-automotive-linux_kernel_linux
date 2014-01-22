/*
 * CSR sirfsoc LCD register def file
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __A7_LCD_H__
#define __A7_LCD_H__

#define S0_HSYNC_PERIOD         0x0000
#define S0_HSYNC_WIDTH          0x0004
#define S0_VSYNC_PERIOD         0x0008
#define S0_VSYNC_WIDTH          0x000c
#define S0_ACT_HSTART           0x0010
#define S0_ACT_VSTART           0x0014
#define S0_ACT_HEND             0x0018
#define S0_ACT_VEND             0x001c
#define S0_OSC_RATIO            0x0020
#define S0_TIM_CTRL             0x0024
#define S0_TIM_STATUS           0x0028
#define S0_HCOUNT               0x002c
#define S0_VCOUNT               0x0030
#define S0_BLANK                0x0034
#define S0_BACK_COLOR           0x0038
#define S0_DISP_MODE            0x003c
#define S0_LAYER_SEL            0x0040
#define S0_RGB_SEQ              0x0044
#define S0_RGB_YUV_COEF1        0x0048
#define S0_RGB_YUV_COEF2        0x004c
#define S0_RGB_YUV_COEF3        0x0050
#define S0_YUV_CTRL             0x0054
#define S0_TV_FIELD             0x0058
#define S0_INT_LINE             0x005c
#define S0_LAYER_STATUS         0x0060
#define S0_RGB_YUV_OFFSET       0x0070

#define LCD_BLS_CTRL1		0x0b00
#define LCD_BLS_CTRL2		0x0b04
#define LCD_BLS_STATUS		0x0b08
#define LCD_CRC_VALUE		0x0b0c
#define LCD_BLS_LEVEL_TB0	0x0b10
#define LCD_BLS_LEVEL_TB1	0x0b14
#define LCD_BLS_LEVEL_TB2	0x0b18
#define LCD_BLS_LEVEL_TB3	0x0b1c

#define DMA_STATUS              0x00f0
#define INT_MASK                0x00f4
#define INT_CTRL_STATUS         0x00f8
#define SCR_CTRL                0x00fc

#define L0_CTRL                 0x0100
#define L0_HSTART               0x0104
#define L0_VSTART               0x0108
#define L0_HEND                 0x010c
#define L0_VEND                 0x0110
#define L0_BASE0                0x0114
#define L0_BASE1                0x0118
#define L0_XSIZE                0x011c
#define L0_YSIZE                0x0120
#define L0_SKIP                 0x0124
#define L0_DMA_CTRL             0x0128
#define L0_ALPHA                0x012c
#define L0_CKEYB_SRC            0x0130
#define L0_CKEYS_SRC            0x0134
#define L0_FIFO_CHK             0x0138
#define L0_FIFO_STATUS          0x013c
#define L0_CKEYB_DST            0x0150
#define L0_CKEYS_DST            0x0154


#define L1_CTRL                 0x0200
#define L1_HSTART               0x0204
#define L1_VSTART               0x0208
#define L1_HEND                 0x020c
#define L1_VEND                 0x0210
#define L1_BASE0                0x0214
#define L1_BASE1                0x0218
#define L1_XSIZE                0x021c
#define L1_YSIZE                0x0220
#define L1_SKIP                 0x0224
#define L1_DMA_CTRL             0x0228
#define L1_ALPHA                0x022c
#define L1_CKEYB_SRC            0x0230
#define L1_CKEYS_SRC            0x0234
#define L1_FIFO_CHK             0x0238
#define L1_FIFO_STATUS          0x023c
#define L1_CKEYB_DST            0x0250
#define L1_CKEYS_DST            0x0254

#define L2_CTRL                 0x0300
#define L2_HSTART               0x0304
#define L2_VSTART               0x0308
#define L2_HEND                 0x030c
#define L2_VEND                 0x0310
#define L2_BASE0                0x0314
#define L2_BASE1                0x0318
#define L2_XSIZE                0x031c
#define L2_YSIZE                0x0320
#define L2_SKIP                 0x0324
#define L2_DMA_CTRL             0x0328
#define L2_ALPHA                0x032c
#define L2_CKEYB_SRC            0x0330
#define L2_CKEYS_SRC            0x0334
#define L2_FIFO_CHK             0x0338
#define L2_FIFO_STATUS          0x033c
#define L2_CKEYB_DST            0x0350
#define L2_CKEYS_DST            0x0354

#define L3_CTRL                 0x0400
#define L3_HSTART               0x0404
#define L3_VSTART               0x0408
#define L3_HEND                 0x040c
#define L3_VEND                 0x0410
#define L3_BASE0                0x0414
#define L3_BASE1                0x0418
#define L3_XSIZE                0x041c
#define L3_YSIZE                0x0420
#define L3_SKIP                 0x0424
#define L3_DMA_CTRL             0x0428
#define L3_ALPHA                0x042c
#define L3_CKEYB_SRC            0x0430
#define L3_CKEYS_SRC            0x0434
#define L3_FIFO_CHK             0x0438
#define L3_FIFO_STATUS          0x043c
#define L3_CKEYB_DST            0x0450
#define L3_CKEYS_DST            0x0454

#define S0_GAMMAFIFO_R          0x0800
#define S0_GAMMAFIFO_G          0x0900
#define S0_GAMMAFIFO_B          0x0a00

#define CUR0_CTRL               0x1000
#define CUR0_HSTART             0x1004
#define CUR0_VSTART             0x1008
#define CUR0_HEND               0x100c
#define CUR0_VEND               0x1010
#define CUR0_COLOR0             0x1014
#define CUR0_COLOR1             0x1018
#define CUR0_COLOR2             0x101c
#define CUR0_COLOR3             0x1020
#define CUR0_ALPHA              0x1024
#define CUR0_FIFO_RDPTR         0x1028
#define CUR0_CURRENT_XY         0x102C
#define CUR0_FIFODATA           0x1400

/**
* Register constant definition
**/
#define LCD_LAYER_REG_SHIFT             8

#define LCD_LAYER_REG_SPACE             (L1_CTRL-L0_CTRL)
#define LCD_LAYER_REG_NUM               (L0_FIFO_STATUS-L0_CTRL) /*Question: Should skip L0_FIFO_STATUS?*/
#define LCD_LAYER_NUM                   0x4


#define LCD_INT_MASK_ALL_OFF        (0x0)
#define LCD_INT_MASK_ALL_ON         (0xffffffff)

/**
* Register bits enum
**/
typedef enum
{
    E_PRIMARY = 0,
    E_OVERLAY_1 = 1,
    E_OVERLAY_2 = 2,
    E_OVERLAY_3 = 3,
    E_LAYER_NUM = 4,
    E_CURSOR = 6, /*Bit 6 of S0_LAYER_STATUS indicate cursor*/
} ENUM_S0_LAYER_SEL;

typedef enum
{
    E_CURSOR_MODE_32x32x2_2_T   = 0,
    E_CURSOR_MODE_32x32x2_4     = 1,
    E_CURSOR_MODE_32x32x2_3_T   = 2,
    E_CURSOR_MODE_64x64x2_2_T   = 4,
    E_CURSOR_MODE_64x64x2_4     = 5,
    E_CURSOR_MODE_64x64x2_3_T   = 6,
    
}ENUM_CUR0_CTRL_MODE;

typedef enum
{
    E_LO_CTRL_BPP_RGB666   = 0,
    E_LO_CTRL_BPP_RGB565   = 1,
    E_LO_CTRL_BPP_RGB556   = 2,
    E_LO_CTRL_BPP_RGB655   = 3,
    E_LO_CTRL_BPP_RGB888   = 4,
    E_LO_CTRL_BPP_TRGB888  = 5,
    E_LO_CTRL_BPP_ARGB8888 = 6,
    E_LO_CTRL_BPP_UNKNOWN  = 7
}ENUM_LO_CTRL_BPP;

typedef enum
{
    E_FORMAT_8_BIT_RBGRBG = 0,
    E_FORMAT_8_BIT_YUV422 = 1,
    E_FORMAT_16BIT_YUV422 = 2,
    E_FORMAT_18BIT_RBG666 = 3,
    E_FORMAT_24BIT_RBG888 = 4
}ENUM_S0_DISP_MODE_OUT_FORMAT;

/**
* Register bit field definitions
**/
typedef union
{
    struct
    {
  DWORD HSYNC_PERIOD:11; /*For master mode, where Prism-lite Processor generates
                        the sync signals, this value defines the period of the
                        horizontal sync pulse, in number of pixels. If this
                        register is set to 60, then the period of the sync is 61
                        pixel clocks. For slave mode, this register has no
                        effect.*/
  DWORD RESERVED    :21;
    } ;
  DWORD DW;
} REG_S0_HSYNC_PERIOD;

typedef union
{
    struct
    {
  DWORD HSYNC_WIDTH :11; /*For master mode, where Prism-lite Processor generates
                        the sync signals, this value defines the width (minus 1)
                        of the horizontal sync pulse, in number of pixels.  If
                        this register is set to 8, then the width of the sync
                        pulse is 9 pixel clocks. For slave mode, this register
                        has no effect.*/
  DWORD RESERVED    :21;
    } ;
  DWORD DW;
} REG_S0_HSYNC_WIDTH;

typedef union
{
    struct
    {
  DWORD VSYNC_PERIOD:11; /*For master mode, where Prism-lite Processor generates
                        the sync signals, this value defines the period of the
                        vertical sync pulse, in number of lines. For slave mode,
                        this register has no effect. The actual vertical line
                        number is VSYNC_PERIOD+1*/
  DWORD RESERVED    :21;
    } ;
  DWORD DW;
} REG_S0_VSYNC_PERIOD;

typedef union
{
    struct
    {
  DWORD VSYNC_WIDTH :12; /*For master mode, where Prism-lite Processor generates
                        the sync signals, this value defines the width of the
                        horizontal sync pulse, in either number of lines or
                        number of pixels (depending on bit 12 above).  For slave
                        mode, this register has no effect.The actual width is
                        VSYNC_WIDTH+1 (lines or pixels)*/
  DWORD WITDTH_UINT :1; /*1 = Vertical sync pulse width defined below is in
                        number of lines.0 = Vertical sync pulse width defined
                        below is in number of pixels.Usually, this bit is set
                        with 1.*/
  DWORD RESERVED    :19;
    } ;
  DWORD DW;
} REG_S0_VSYNC_WIDTH;

typedef union
{
    struct
    {
  DWORD ACT_HSTART  :11; /*Horizontal Start Position (in pixel number).This
                        value, along with the horizontal end position and
                        vertical start and end positions, define the rectangle
                        region of active region.*/
  DWORD RESERVED    :21;
    } ;
  DWORD DW;
} REG_S0_ACT_HSTART;

typedef union
{
    struct
    {
  DWORD ACT_VSTART  :11; /*Vertical Start Position (in line number).*/
  DWORD RESERVED    :21;
    } ;
  DWORD DW;
} REG_S0_ACT_VSTART;

typedef union
{
    struct
    {
  DWORD ACT_HEND    :12; /*Horizontal End Position (in pixel number).When work
                        at 8-bit RGB mode,=ACT_HSTART + (WIDTH - 1)*3When work
                        at 8-bit YUV mode,=ACT_HSTART + (WIDTH - 1)*2Others=
                        ACT_HSTART + WIDTH - 1*/
  DWORD RESERVED    :20;
    } ;
  DWORD DW;
} REG_S0_ACT_HEND;

typedef union
{
    struct
    {
  DWORD ACT_VEND    :11; /*Vertical End Position (in line number).
                        = ACT_VSTART + DEPTH - 1*/
  DWORD RESERVED    :21;
    } ;
  DWORD DW;
} REG_S0_ACT_VEND;

typedef union
{
    struct
    {
  DWORD DIV_RATIO   :10; /*The pixel clock is divided from the system clock when
                        Prism-lite drives the pixclk:
                        Fpixclk = Fsys_clk / (DIV_RATIO+1) The minimum DIV_RATIO
                        value is 1.*/
  DWORD RESERVED0   :2;
  DWORD HALF_DUTY   :1; /*1 = Generate pixclk of 50% duty cycle when the divider
                        ratio (DIV_RATIO<9:0>) is even.
                        0 = Generate pixclk is not 50% duty cycle clock when the
                        divider ratio (DIV_RATIO<9:0>) is even. */
  DWORD RESERVED1   :3;
  DWORD PCLK_CTRL   :1; /* Pixel clock stop control bit
                        0: pixel clock work normal
                        1: Pixel clock stops when layer FIFO is about to be underflow*/
  DWORD RESERVED2   :15; 
    } ;
  DWORD DW;
} REG_S0_OSC_RATIO;

typedef union
{
    struct
    {
  DWORD RESERVED0   :1;
  DWORD PCLK_IO     :1; /*Pixel clock master mode1 = Prism-lite Processor drives
                        pixel clock0 = display drives pixel clock*/
  DWORD PCLK_POLAR  :1; /*Invert pixel clock.  In master mode, invert the
                        internal pixel clock before output.  In slave mode,
                        invert input pixel clock before use by internal logic.
                        1 = Invert pixel clock (i.e. pixel clock ? phase off)
                        0 = Do not invert pixel clock*/
  DWORD PCLK_EDGE   :1; /*Determines whether pixel output changes on the rising
                        or falling edge of the internal pixel clock. 1 = Pixel
                        changes on rising edge of clock.0 = Pixel changes on
                        falling edge of clock.*/
  DWORD HSYNC_IO    :1; /*Horizontal sync signal master mode:1 = Prism-lite
                        Processor drives horizontal sync0 = display drives
                        horizontal sync*/
  DWORD HSYNC_POLAR :1; /*Invert the horizontal sync signal.  In master mode,
                        invert the horizontal sync signal before output.  In
                        slave mode, invert the horizontal sync signal before
                        used by internal logic.1 = horizontal sync signal is
                        active low.0 = horizontal sync signal is active high.*/
  DWORD VSYNC_IO    :1; /*Vertical sync signal master mode:
                        1 = Prism-lite Processor drives vertical sync
                        0 = display drives vertical sync*/
  DWORD VSYNC_POLAR :1; /*Invert the vertical sync signal.  In master mode,
                        invert the vertical sync signal before output.
                        In slave mode, invert the vertical sync signal before
                        used by internal logic.
                        1 = vertical sync signal is active low.
                        0 = vertical sync signal is active high.*/
  DWORD PCLK_MASK   :1; /*Test Purpose register, software writes this with 0
                        Remark:Mask pixel clock control1:
                        1 = Pixel clock is masked to 0 whenever the pixel output
                        data are invalid.
                        Note: This signal only masks the pixel clock to the
                        external pin.  The internal pixel clock is not masked,
                        so logic operation remains the same.
                        Note: Masking the pixclk is useful for certain display
                        modes, such as STN displays*/
  DWORD HSYNC_MASK  :1; /*Test Purpose register, software writes this with 0
                        Remark: Mask HSYNC control:
                        1 = Disable the HSYNC during vertical blank time.
                        0 = Enable the HSYNC during vertical blank time.*/
  DWORD SYNC_DLY    :3; /*Delay the Hsync, Vsync and bias output for the number
                        of system clocks.  (range from 1 to 7)It only delays the
                        signals during one pixel clock to satisfy the setup
                        or hold time requirement.*/
  DWORD RESERVED1   :19;
    } ;
  DWORD DW;
} REG_S0_TIM_CTRL;

typedef union
{
    struct
    {
  DWORD RGB_SEQ_STA :2; /*Current value of the RGB select index, for choosing
                        which color to output based on the RGB sequence (for
                        displays which require outputs of a single color at a
                        time).*/
  DWORD VSYNC_STA   :1; /*Current value of the internal vertical sync signal*/
  DWORD HSYNC_STA   :1; /*Current value of the internal horizontal sync signal*/
  DWORD PCLK_STA    :1; /*Current value of internal pixel clock.*/
  DWORD RESERVED0   :27;
    } ;
  DWORD DW;
} REG_S0_TIM_STATUS;

typedef union
{
    struct
    {
  DWORD HCOUNT      :11; /*Current value of the internal horizontal counter*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_S0_HCOUNT;

typedef union
{
    struct
    {
  DWORD VCOUNT      :11; /*Current value of the internal vertical counter*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_S0_VCOUNT;

typedef union
{
    struct
    {
  DWORD BLANK_VALUE :24; /*Pixel value to be used for the inactive region:
                        Bits 23:16 - R valueBits
                        15:8 - G valueBits
                        7:0 - B value*/
  DWORD BLANK_VALID :1; /*Use blank value1 = Use blank value defined below when
                        not in active region.
                        0 = Display the last pixel value when not in active
                        region*/
  DWORD RESERVED0   :7;
    } ;
  DWORD DW;
} REG_S0_BLANK;

typedef union
{
    struct
    {
  DWORD Background_B:8;
  DWORD Background_G:8;
  DWORD Background_R:8;
                            /*Pixel value to be used for the background color:
                            Bits 23:16 - R valueBits
                            15:8 - G valueBits
                            7:0 - B value*/
  DWORD RESERVED        :8;
    } ;
  DWORD DW;
} REG_S0_BACK_COLOR;

typedef union
{
    struct
    {
  DWORD FRAME_VALID :1; /*Frame valid. When this bit is set, the frame is
                        considered valid; the valid counter will be generated.
                        When this bit is cleared, the frame is considered
                        invalid and the counters do not change, and thus there
                        is no output.  Note that when the bit value changes in
                        the middle of a frame, it does not take effect until the
                        beginning of the next frame, i.e. there is a vertical
                        sync signal. Normally this bit should be true.*/
  DWORD OUT_FORMAT  :3; /*Output format:
                        000: 8-bit rgbrgb
                        001: 8-bit yuv422
                        010: 16bit yuv422
                        011: 18-bit rgb666
                        100: 24-bit rgb888*/
  DWORD TOP_LAYER   :2; /*It decides which layer will be the top layer.
                        000:  layer0
                        001:  layer1
                        010:  layer2
                        011:  layer3*/
  DWORD RESERVED0   :1;
  DWORD GAMMA_COR_EN:1; /*0: Bypass gamma correcion function
                          1: Using gamma correction function*/
  DWORD RESERVED1   :24;
    } ;
  DWORD DW;
} REG_S0_DISP_MODE;

typedef union
{
    struct
    {
  DWORD LAYER_SEL   :8; /*Bit0: layer0 is enabled.
                        Bit1: layer1 is enabled.
                        Bit2: layer2 is enabled.
                        Bit3: layer3 is enabled.
                        Bit6: cursor0 is enabled.If set the corresponding bit,
                        the corresponding layer is enabled. If user want
                        reconfigure one of the 5 layers, please disable it
                        first, then wait until corresponding bit of LAYER_STATUS
                        changed to 0.Only after the respective layer are
                        enabled, LCD controller will read data from memory and
                        send data to LCD panel.*/
  DWORD RESERVED0   :24;
    } ;
  DWORD DW;
} REG_S0_LAYER_SEL;

typedef union
{
    struct
    {
  DWORD EVEN_RGBSEQ :6; /*These bits represent the RGB output sequence for
                        even-number lines, the meaning is the same as
                        ODD_RGBSEQ.*/
  DWORD ODD_RGBSEQ  :6; /*For displays requiring only one color per pixel,
                        these bits represent the RGB output sequence for the
                        odd-number lines.  Two bits each represent a color:
                        00 - Red
                        01 - Green
                        10 - Blue
                        For example, 000110 means the sequence is R-G-B-R-G-B,
                        and 100100 represent B-G-R-B-G-R, etc.For displays
                        requiring all three color values per pixel, this
                        register has no effect.*/
  DWORD RESERVED0   :20;
    } ;
  DWORD DW;
} REG_S0_RGB_SEQ;

typedef union
{
    struct
    {
  DWORD COEF3       :8; /*Coefficient 13 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD COEF2       :8; /*Coefficient 12 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD COEF1       :8; /*Coefficient 11 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_S0_RGB_YUV_COEF1; /*0x00428119*/

typedef union
{
    struct
    {
  DWORD COEF3       :8; /*Coefficient 23 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD COEF2       :8; /*Coefficient 22 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD COEF1       :8; /*Coefficient 21 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_S0_RGB_YUV_COEF2; /*0x00264A70*/

typedef union
{
    struct
    {
  DWORD COEF3       :8; /*Coefficient 33 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD COEF2       :8; /*Coefficient 32 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD COEF1       :8; /*Coefficient 31 for the RGB to YUV conversion matrix.
                        This value should be an 8-bit unsigned value. */
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_S0_RGB_YUV_COEF3; /*0x00705E12*/

typedef union
{
    struct
    {
  DWORD RESERVED2  :3;
  DWORD RGB_S_CONV  :3; /*Invert the MSB of the RGB components to convert signed
                        to unsigned when the input RGB is signed
                        Bit 5 - invert R
                        Bit 4 - invert G
                        Bit 3 - invert B
                        Please set this bit to 3¡¯b000*/
  DWORD YUV_SEQ     :2; /*For 8-bit 4:2:2 output,
                        00:  send data as YUYV sequence.
                        01:  send data as YVYU sequence.
                        10:  send data as UYVY sequence.
                        11:  send data as VYUY sequence.*/
  DWORD RGB_YUV     :1; /*1 = Do RGB to YUV conversion
                        0 = Bypass RGB to YUV conversion*/
  DWORD Even_UV     :1; /*1=Keep the even pixel UV (U1V1 mode)
                        0=keep the even pixel U and odd pixel V(U1V2 mode)*/
  DWORD RESERVED0   :2;
  DWORD EVENFIELD   :1; /*This bit only used when in TV mode:
                        1=Current field is the even field of a frame
                        0=Current field is the odd field of a frame*/
  DWORD RESERVED1   :9;
    } ;
  DWORD DW;
} REG_S0_YUV_CTRL;

typedef union
{
    struct
    {
  DWORD TV_HSTART   :11; /*Start pixel number for the active TV display.*/
  DWORD RESERVED0   :1;
  DWORD TV_VSTART   :11; /*Vertical Start Position (in line number) for the
                        active TV display.*/
  DWORD RESERVED1   :1;
  DWORD TV_F_VALID  :1; /*1 = Work at TV mode. When at master mode, the
                        TV_HASTART and TV_VSTART will work.
                        0 = Work at normal mode.This is used when doing TV 2
                        fields mode - define the EVEN fields position*/
  DWORD RESERVED2   :7;
    } ;
  DWORD DW;
} REG_S0_TV_FIELD;

typedef union
{
    struct
    {
  DWORD LINE_NUM    :11; /*This value represents the line number at which a
                        screen interrupt will be generated. This allows the user
                        to specify a line during which an interrupt will be
                        generated for each frame. The interrupt is generated at
                        the beginning of that particular line.*/
  DWORD RESERVED0   :1;
  DWORD INT_LINE_VALID:1; /*This bit determines whether the line number
                        LINE_NUM is valid for generating a screen interrupt.
                        This differs from the mask in that if this bit is not
                        set, then the interrupt status bit for the screen
                        interrupt will not be set. When this bit is set, then
                        the interrupt status bit is set each time the line count
                        is equal to the value below.*/
  DWORD RESERVED1   :18;
                        
  DWORD INT_TV_MODE :1; /*This bit indicates whether line interrupt generated for only even field or for both even and odd fields.
                        0: for only even fields in TV mode
                        1: for both even/odd fields in TV mode
                        For progressive display mode, this bit takes no effect.*/
    } ;
  DWORD DW;
} REG_S0_INT_LINE;

/*
** The status will changed at the next frame
** after you configure LCD_S0_LAYER_SEL register.
*/
typedef union
{
    struct
    {
  DWORD LAYER_0     :1; /*1: this layer is enabled.
                        0: this layer is disabled.
                        The status will changed at the next frame after you
                        configure LCD_S0_LAYER_SEL register.*/
  DWORD LAYER_1     :1;
  DWORD LAYER_2     :1;
  DWORD LAYER_3     :1;
  DWORD RESERVED0   :2;
  DWORD CURSOR      :1; /*Cursor enable status*/
  DWORD RESERVED1   :25;
    } ;
  DWORD DW;
} REG_S0_LAYER_STATUS;

typedef union
{
    struct
    {
  DWORD V_OFFSET     :8; /* 128, unsigned int 8 */
  DWORD U_OFFSET     :8; /* 128, unsigned int 8 */
  DWORD Y_OFFSET     :8; /* 16,  unsigned int 8 */
  DWORD RESERVED1    :8;
    } ;
  DWORD DW;
} REG_S0_RGB_YUV_OFFSET; /*0x00108080*/

typedef union
{
    struct
    {
  DWORD FIFO_DATA   :32; /*Values in the gamma FIFO. Can directly read or write
                        values anywhere in the FIFO by specifying the
                        corresponding address. 256 entries totally. 
                        First 256 entries for R */
    } ;
  DWORD DW;
} REG_S0_GAMMAFIFO;


typedef union
{
    struct
    {
  DWORD L0_DMA_STATUS   :1; /*0: No DMA operation; 1: DMA is running*/
  DWORD L1_DMA_STATUS   :1;
  DWORD L2_DMA_STATUS   :1;
  DWORD L3_DMA_STATUS   :1;
  DWORD RESERVED1   :28;
    } ;
  DWORD DW;
} REG_DMA_STATUS;

typedef union
{
    struct
    {
  DWORD L0_DMA_MASK :1; /*One frame DMA over mask*/
  DWORD L1_DMA_MASK :1;
  DWORD L2_DMA_MASK :1;
  DWORD L3_DMA_MASK :1;
  DWORD RESERVED0   :2;
  DWORD L0_OFLOW_MASK   :1; /*Screen FIFO Overflow mask*/
  DWORD L1_OFLOW_MASK   :1;
  DWORD L2_OFLOW_MASK   :1;
  DWORD L3_OFLOW_MASK   :1;
  DWORD RESERVED1   :2;
  DWORD L0_UFLOW_MASK   :1; /*Screen FIFO Underflow mask*/
  DWORD L1_UFLOW_MASK   :1;
  DWORD L2_UFLOW_MASK   :1;
  DWORD L3_UFLOW_MASK   :1;
  DWORD RESERVED2   :2;
  DWORD S0_LINE_INT_MASK    :1; /*Line interrupt*/
  DWORD RESERVED3   :9;
  DWORD L0_UNFINISH_MASK   :1; /*Not finish Interrupt mask*/
  DWORD L1_UNFINISH_MASK   :1;
  DWORD L2_UNFINISH_MASK   :1;
  DWORD L3_UNFINISH_MASK   :1;
    } ;
  DWORD DW;
} REG_INT_MASK;

typedef union
{
    struct
    {
  DWORD L0_DMA_INT  :1; /*One frame DMA over Interrupt*/
  DWORD L1_DMA_INT  :1;
  DWORD L2_DMA_INT  :1;
  DWORD L3_DMA_INT  :1;
  DWORD RESERVED0   :2;
  DWORD L0_OFLOW_INT    :1; /*Screen FIFO Overflow Interrupt*/
  DWORD L1_OFLOW_INT    :1;
  DWORD L2_OFLOW_INT    :1;
  DWORD L3_OFLOW_INT    :1;
  DWORD RESERVED1   :2;
  DWORD L0_UFLOW_INT    :1; /*Screen FIFO Underflow Interrupt*/
  DWORD L1_UFLOW_INT    :1;
  DWORD L2_UFLOW_INT    :1;
  DWORD L3_UFLOW_INT    :1;
  DWORD RESERVED2   :2;
  DWORD S0_LINE_INT_INT :1; /*Line interrupt*/
  DWORD RESERVED3   :9;
  DWORD L0_UNFINISH_INT   :1; /*Not finish Interrupt*/
  DWORD L1_UNFINISH_INT   :1;
  DWORD L2_UNFINISH_INT   :1;
  DWORD L3_UNFINISH_INT   :1;
    };
  DWORD DW;
} REG_INT_CTRL_STATUS;


typedef union
{
    struct
    {
  DWORD SCREEN0_EN  :1; /*1: This screen is enabled.
                        0: This screen is disabled.
                        When work at master mode, after enable this bit,
                        the internal counters will count based on the pixel
                        clocks and horizontal sync signals.*/
  DWORD EN_DELAY_MODE :1; /* A6 only feature. Screen enable delay mode
			  1: the first frame will be sent out as soon as SCREEN_EN is set.
			  0: the first frame will be sent out with a delay of one frame's interval after SCREEN_EN is set.*/
  DWORD RESERVED0   :30;
    } ;
  DWORD DW;
} REG_SCR_CTRL;

/*
** Layer Registers
*/
typedef union
{
    struct
    {
  DWORD BPP         :3; /*000 - 18-bit per pixel, RGB666
                        001 - 16-bit per pixel, RGB565
                        010 - 16-bit per pixel, RGB556
                        011 - 16-bit per pixel, RGB655
                        100 - 32-bit per pixel, RGB888
                        101 - 32-bit per pixel, TRGB888
                        110 - 32-bit per pixel, ARGB8888
                        111 - Reserved. */
  DWORD RESERVED2   :1; /* REMOVED */
  DWORD RESERVED1   :1; /* REMOVED */
  DWORD FIFO_RESET  :1; /*Set this bit will reset the FIFO write and read pointer.*/
  DWORD SRC_CKEY_EN     :1; /*Enable the color key function.*/
  DWORD FIFO_FKRDY   :1;
  DWORD CONFIRM     :1; /*Confirm all the setting of this layer. This bit must
                        be set last after all other register has been configured.
                        This bit will self-clear after configuration valid.*/
  DWORD GLOBAL_ALPHA :1; /* Enable const alpha blend.
                        0: Alpha is from constant alpha
                        1: Alpha is from surface. If the surface format has no 
                        alpha value, default value equals to 1.0. */
  DWORD REPLICATE   :1; /* to convert format to more bits 
                        0: fill 0 to LSB 
                        1: fill MSB to LSB */
  DWORD DST_CKEY_EN :1; /*Enable the dst color key function.*/
  DWORD PREMULTI_ALPHA :1; /* Indicate if source RGB data has been pre-multiplied by alpha:
                           1: pre-multiplied
                           0: not pre-multiplied 
                           driver is required to set it to 1 for non-ARGB format */
  DWORD SOURCE_ALPHA :1; /* Source alpha function enable:
                         1: enable
                         0: disable */
  DWORD RESERVED    :18;
    } ;
  DWORD DW;
} REG_L0_CTRL;

typedef union
{
    struct
    {
  DWORD HSTART   :12; /*Horizontal Start Position (in pixel number). This
                        value, along with the horizontal end position and
                        vertical start and end positions, define the rectangle
                        region of layer0.
                        When work at 8-bit RGB mode,
                        L0_HSTART - SCREEN_HSTART must be the multiple of 3.
                        When work at 8-bit YUV mode,
                        L0_HSTART - SCREEN_HSTART must be the multiple of 2.*/
  DWORD RESERVED0   :20;
    } ;
  DWORD DW;
} REG_L0_HSTART;

typedef union
{
    struct
    {
  DWORD VSTART   :11; /*Vertical Start Position (in line number)*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_L0_VSTART;

typedef union
{
    struct
    {
  DWORD HEND     :12; /*Horizontal End Position (in pixel number).
                        When work at 8-bit RGB mode,
                        =L0_HSTART + (WIDTH - 1)*3
                        When work at 8-bit YUV mode,
                        =L0_HSTART + (WIDTH - 1)*2
                        Others=L0_HSTART + WIDTH - 1*/
  DWORD RESERVED0   :20;
    } ;
  DWORD DW;
} REG_L0_HEND;
typedef union
{
    struct
    {
  DWORD VEND     :11; /*Vertical End Position (in line number).
                        =L0_VSTART + DEPTH - 1*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_L0_VEND;

typedef union
{
    struct
    {
  DWORD BASE0_ADDR  :32; /*DMA address of the starting memory location for the
                        screen data, this is a byte address, but the lower 3
                        address bits must be zeroes, which means the DMA start
                        address must lie on a qwords burst boundary.*/
    } ;
  DWORD DW;
} REG_L0_BASE0;

typedef union
{
    struct
    {
  DWORD BASE1_ADDR  :32; /*DMA address of the second starting memory location
                        for the screen data, this is a byte address, but the
                        lower 3 address bits must be zeroes, which means the DMA
                        start address must lie on a qwords burst boundary.*/
    } ;
  DWORD DW;
} REG_L0_BASE1;

typedef union
{
    struct
    {
  DWORD XSIZE    :13; /*This value specifies the number of consecutive bursts
                        per line for the screen DMA.
                        If (ByteWidthOfL0FrameBuf % ((dma_unit+1)*8))
                        L0_XSIZE = ByteWidthOfL0FrameBuf /((dma_unit+1)*8)
                        Else
                        L0_XSIZE = ByteWidthOfL0FrameBuf /((dma_unit+1)*8) - 1*/
  DWORD RESERVED0   :19;
    } ;
  DWORD DW;
} REG_L0_XSIZE;

typedef union
{
    struct
    {
  DWORD YSIZE    :13; /*This value specifies the number of "lines" for the
                        screen DMA. Each line designates a segment of
                        consecutive QWORDs with a skip in between.
                        L0_YSIZE = DEPTH - 1*/
  DWORD RESERVED0   :19;
    } ;
  DWORD DW;
} REG_L0_YSIZE;

typedef union
{
    struct
    {
  DWORD SKIP     :13; /*This value specifies the number of BYTEs for the DMA
                        address generator to skip in between lines of the screen
                        DMA.
                        L0_SKIP = ByteWidthOfL0FrameBuf - (L0_XSIZE*(DMA_UNIT+1)*8)
                        And the ByteWidthofL0FrameBuf must be a multiple of 8*/
  DWORD RESERVED0   :19;
    } ;
  DWORD DW;
} REG_L0_SKIP;


typedef union
{
    struct
    {
  DWORD RESERVE2    :1;
  DWORD DMA_MODE    :1; /*Continuous mode DMA
                        1 = when this DMA completes, will automatically generate
                        a DMA with exactly the same setting.
                        Outdated. Please always write 1 to this bit*/
  DWORD DMA_CHAIN_MODE:1;/*Chain DMA mode
                        1 = Enable the DMA chain mode.
                        0 = Disable the DMA chain mode.*/
  DWORD RESERVED0   :1;
  DWORD DMA_UNIT    :4; /*Burst unit for one dma operation, only support
                        following configure:
                        4'h3:   4 QWORD
                        4'h7:   8 QWORD
                        4'hF:  16 QWORD*/
  DWORD SUPPRESS_QW_NUM :4;/*The number of QWORD writes to the FIFO that must
                        be suppressed at the end of each DMA line.*/
  DWORD RESERVED1   :18;
  DWORD DMA_HURRY   :1; /* DMA HURRY if underflow happens
                        0: no effect
                        1: DMA finishes as soon as possible for this layer when underflow happens*/
  DWORD VPP_PASS_MODE   :1;
  
    };
  DWORD DW;
} REG_L0_DMA_CTRL;

typedef union
{
    struct
    {
  DWORD ALPHA_VAL   :8; /*8-bit planar alpha value to blend all pixels on L0
                        layer.*/
  DWORD RESERVED0   :24;
    } ;
  DWORD DW;
} REG_L0_ALPHA;

typedef union
{
    struct
    {
  DWORD B   :8;
  DWORD G   :8;
  DWORD R   :8; /*Bigger value of color key.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_L0_CKEYB_SRC;

typedef union
{
    struct
    {
  DWORD B   :8;
  DWORD G   :8;
  DWORD R   :8; /*Smaller value of color key.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_L0_CKEYS_SRC;

typedef union
{
    struct
    {
  DWORD B   :8;
  DWORD G   :8;
  DWORD R   :8; /*Bigger value of color key.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_L0_CKEYB_DST;

typedef union
{
    struct
    {
  DWORD B   :8;
  DWORD G   :8;
  DWORD R   :8; /*Smaller value of color key.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_L0_CKEYS_DST;

typedef union
{
    struct
    {
  DWORD L0_LO_CHK  :8; /* Low request watermark of the layer FIFO
                       The maximum of this value is (0x100 - (dma_unit + 1))*/
  DWORD L0_MI_CHK  :8; /* Middle request watermark of the layer FIFO
                       The maximum of this value is (0x100 - 2*(dma_unit + 1)) */
  DWORD RESERVED0   :8;
  DWORD L0_REQ_SEL  :1; /* This register is for selecting a request generation method for L0 FIFO:
                        1:  Middle request and low request watermark are useful; using this mode will save bandwidth
                        0:  Normal request generation method, only low request watermark are useful */
  DWORD RESERVED1   :7;
    } ;
  DWORD DW;
} REG_L0_FIFO_CHK;

typedef union
{
    struct
    {
  DWORD FIFO_LEN    :8; /*If this register is read, the valid FIFO length will
                        be given.*/
  DWORD RESERVED0   :24;
    } ;
  DWORD DW;
} REG_L0_FIFO_STATUS;

/*
** Cursor Registers
*/
typedef union
{
    struct
    {
  DWORD MODE        :3; /*000: 32x32x2bpp 2-color and transparency mode
                        001: 32x32x2bpp 4-color mode
                        010: 32x32x2bpp 3-color and transparency mode
                        011: reserved.
                        100: 64x64x2bpp 2-color and transparency mode
                        101: 64x64x2bpp 4-color mode
                        110: 64x64x2bpp 3-color and transparency mode*/
  DWORD RESERVED0   :1;
  DWORD DWORD_BLE   :1; /*Big/Little Endian selection of byte for image data.
                        1 = Little Endian:MSB- byte3, byte2, byte1, byte0-LSB
                        0 = Big Endian:  MSB- byte0, byte1, byte2, byte3-LSB*/
  DWORD BYTE_BLE    :1; /*Big/Little Endian selection of pixel data for each byte.
                        1 = Little Endian:MSB- P3, P2, P1, P0-LSB
                        0 = Big Endian:  MSB- P0, P1, P2, P3-LSB*/
  DWORD RESERVED1   :2;
  DWORD SRAM_ADDRST :1; /*Soft reset of cursor0 SRAM addresses:
                        1 = Setting this bit to 1 will reset the read and the write
                        address of SRAM. It can be used for recovery from abnormal operations.
                        0 = After reset, this bit should be set back to 0 for normal operation.*/
  DWORD RESERVED2   :7;
  DWORD SETTING_VALID:1; /*Confirm the new Region and other setting by writing
                        this bit with 1. This bit is self-cleared after
                        detecting a valid frame start. Remark:This bit is used
                        as the confirmation of the setting-group.*/
  DWORD RESERVED3   :15;
    } ;
  DWORD DW;
} REG_CUR0_CTRL;

typedef union
{
    struct
    {
  DWORD HSTART :11; /*Horizontal Start Position (in pixel number). This
                        value, along with the horizontal end position and
                        vertical start and end positions, define the rectangle
                        region of layer0. This value must be greater or equal to
                        the ACT_HSTART.*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_CUR0_HSTART;

typedef union
{
    struct
    {
  DWORD VSTART :11; /*Vertical Start Position (in line number). This value
                        must be greater or equal to the ACT_VSTART*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_CUR0_VSTART;

typedef union
{
    struct
    {
  DWORD HEND   :11; /*Horizontal End Position (in pixel number). This value
                        must be less or equal to the ACT_HEND*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_CUR0_HEND;

typedef union
{
    struct
    {
  DWORD VEND   :11; /*Vertical End Position (in line number). This value
                        must less or equal the ACT_VEND*/
  DWORD RESERVED0   :21;
    } ;
  DWORD DW;
} REG_CUR0_VEND;

typedef union
{
    struct
    {
  DWORD BLUE        :8; /*Blue value for the color0 of cursor0.*/
  DWORD GREEN       :8; /*Green value for the color0 of cursor0.*/
  DWORD RED         :8; /*Red value for the color0 of cursor0.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_CUR0_COLOR0;

typedef union
{
    struct
    {
  DWORD BLUE        :8; /*Blue value.*/
  DWORD GREEN       :8; /*Green value.*/
  DWORD RED         :8; /*Red value.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_CUR0_COLOR1;

typedef union
{
    struct
    {
  DWORD BLUE        :8; /*Blue value.*/
  DWORD GREEN       :8; /*Green value.*/
  DWORD RED         :8; /*Red value.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_CUR0_COLOR2;

typedef union
{
    struct
    {
  DWORD BLUE        :8; /*Blue value.*/
  DWORD GREEN       :8; /*Green value.*/
  DWORD RED         :8; /*Red value.*/
  DWORD RESERVED0   :8;
    } ;
  DWORD DW;
} REG_CUR0_COLOR3;

typedef union
{
    struct
    {
  DWORD ALPHA_VAL   :8; /*8-bit planar alpha value to blend all pixels on
                        cursor0 layer.*/
  DWORD RESERVED0   :24;
    } ;
  DWORD DW;
} REG_CUR0_ALPHA;

typedef union
{
    struct
    {
  DWORD FIFO_RDPTR  :8; /*Read pointer of SRAM.*/
  DWORD RESERVED0   :24;
    } ;
  DWORD DW;
} REG_CUR0_FIFO_RDPTR;

typedef union
{
    struct
    {
  DWORD CUR_X       :7; /*X location of cursor active region.*/
  DWORD RESERVED0   :9;
  DWORD CUR_Y       :7; /*Y location of cursor active region.*/
  DWORD RESERVED1   :9;
    } ;
  DWORD DW;
} REG_CUR0_CURRENT_XY;

typedef union
{
    struct
    {
  DWORD FIFO_DATA   :32; /*Values in the cursor FIFO. Can directly read or write
                        values anywhere in the FIFO by specifying the
                        corresponding address.*/
    } ;
  DWORD DW;
} REG_CUR0_FIFODATA;

#endif  // __LCD_H__
