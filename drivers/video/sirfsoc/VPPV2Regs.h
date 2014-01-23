/*
 * CSR sirfsoc VPP register def file
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __A7_VPP_H__
#define __A7_VPP_H__

#define VPP_CTRL		0x0000 
#define VPP_YBASE		0x0004 
#define VPP_UBASE		0x0008 
#define VPP_VBASE		0x000C        
#define VPP_DESBASE		0x0010 
#define VPP_WIDTH		0x0014 
#define VPP_HEIGHT		0x0018 
#define VPP_STRIDE0		0x001c 
#define VPP_STRIDE1		0x0020 
#define VPP_HSCA_COEF00		0x0024 
#define VPP_HSCA_COEF01 	0x0028 
#define VPP_HSCA_COEF02 	0x002c 
#define VPP_HSCA_COEF10 	0x0030 
#define VPP_HSCA_COEF11 	0x0034 
#define VPP_HSCA_COEF12 	0x0038 
#define VPP_HSCA_COEF20 	0x003c 
#define VPP_HSCA_COEF21 	0x0040 
#define VPP_HSCA_COEF22 	0x0044 
#define VPP_HSCA_COEF30 	0x0048 
#define VPP_HSCA_COEF31 	0x004c 
#define VPP_HSCA_COEF32 	0x0050 
#define VPP_HSCA_COEF40 	0x0054 
#define VPP_HSCA_COEF41 	0x0058 
#define VPP_HSCA_COEF42 	0x005c 
#define VPP_HSCA_COEF50 	0x0060 
#define VPP_HSCA_COEF51 	0x0064 
#define VPP_HSCA_COEF52 	0x0068 
#define VPP_HSCA_COEF60 	0x006c 
#define VPP_HSCA_COEF61 	0x0070 
#define VPP_HSCA_COEF62 	0x0074 
#define VPP_HSCA_COEF70 	0x0078 
#define VPP_HSCA_COEF71		0x007c
#define VPP_HSCA_COEF72 	0x0080 
#define VPP_HSCA_COEF80 	0x0084 
#define VPP_HSCA_COEF81 	0x0088 
#define VPP_HSCA_COEF82 	0x008c 
#define VPP_VSCA_COEF00 	0x0090 
#define VPP_VSCA_COEF01 	0x0094 
#define VPP_VSCA_COEF10 	0x0098 
#define VPP_VSCA_COEF11 	0x009c 
#define VPP_VSCA_COEF20 	0x00a0 
#define VPP_VSCA_COEF21 	0x00a4 
#define VPP_VSCA_COEF30 	0x00a8 
#define VPP_VSCA_COEF31 	0x00ac 
#define VPP_VSCA_COEF40 	0x00b0 
#define VPP_VSCA_COEF41 	0x00b4 
#define VPP_VSCA_COEF50 	0x00b8 
#define VPP_VSCA_COEF51 	0x00bc 
#define VPP_VSCA_COEF60 	0x00c0 
#define VPP_VSCA_COEF61 	0x00c4 
#define VPP_VSCA_COEF70 	0x00c8 
#define VPP_VSCA_COEF71 	0x00cc 
#define VPP_VSCA_COEF80 	0x00d0 
#define VPP_VSCA_COEF81 	0x00d4 
#define VPP_RCOEF		0x00d8 
#define VPP_GCOEF		0x00dc 
#define VPP_BCOEF		0x00e0 
#define VPP_OFFSET1		0x00e4 
#define VPP_OFFSET2		0x00e8 
#define VPP_OFFSET3		0x00ec 
#define VPP_INT_MASK		0x00f0 
#define VPP_INT_STATUS		0x00f4 
#define VPP_ACC			0x00f8 
#define VPP_FULL_THRESH		0x00fc

#define VPP_COLOR_HS_CTRL 0x100	
#define	VPP_COLOR_BC_CTRL 0x104
#define	VPP_YBASE_BOT 0x108
#define	VPP_UBASE_BOT 0x10c
#define	VPP_VBASE_BOT 0x110
#define	VPP_DESBASE_BOT 0x114


/**
* Register constant definition
**/
#define VPP_HSCA_REG_SPACE		((VPP_HSCA_COEF82-VPP_HSCA_COEF00)/4+1)
#define VPP_VSCA_REG_SPACE		((VPP_VSCA_COEF81-VPP_VSCA_COEF00)/4+1)

#define VPP_LAYER_REG_NUM		(VPP_DESBASE_ADDR_BOT+4)

/**
* Register bits enum
**/
typedef enum
{
    E_VPP_YUV422_FORMAT_YUYV = 0,
    E_VPP_YUV422_FORMAT_YVYU = 1,
    E_VPP_YUV422_FORMAT_UYVY = 2,
    E_VPP_YUV422_FORMAT_VYUY = 3,
} ENUM_VPP_YUV422_FORMAT;

typedef enum
{
    E_VPP_OUT_FORMAT_RGB565 = 0,
    E_VPP_OUT_FORMAT_RGB666 = 1,
    E_VPP_OUT_FORMAT_RGB888 = 2,
    E_VPP_OUT_FORMAT_YUV422 = 3,
} ENUM_VPP_OUT_FORMAT;

typedef enum
{
    E_VPP_PIXEL_FORMAT_YUV422 = 0,
    E_VPP_PIXEL_FORMAT_YUV420 = 1,
} ENUM_VPP_PIXEL_FORMAT;

typedef enum
{
    E_VPP_ENDIAN_MODE_LITTLE = 0,
    E_VPP_ENDIAN_MODE_BIG = 1,
} ENUM_VPP_ENDIAN_MODE;

typedef enum
{
    E_VPP_DEST_MEMORY = 0,
    E_VPP_DEST_LCD = 1,
} ENUM_VPP_DEST;

typedef enum
{
    E_VPP_SEQ_TYPE_PIPO = 0,
    E_VPP_SEQ_TYPE_PIIO = 1, 
    E_VPP_SEQ_TYPE_IIPO = 2,
    E_VPP_SEQ_TYPE_IIIO = 3,
} ENUM_VPP_SEQ_TYPE;

typedef enum
{
    E_VPP_HW_DI_MODE_RESERVED = 0,
    E_VPP_HW_DI_MODE_WEAVE = 1,
    E_VPP_HW_DI_MODE_3MEDIAN = 2,
    /* Vertical Median Ranking Interpolation */
    E_VPP_HW_DI_MODE_VMRI = 3,    
} ENUM_VPP_HW_DI_MODE;

/**
* Register bit field definitions
**/
typedef union
{
    struct
    {
  DWORD PIXEL_FORMAT:1; /*Input video format:
0: YUV422 format
1: YUV420 format
*/
  DWORD ENDIAN_MODE:1;/*Indicates the endian mode of YUV422 format:
0: Little endian (Y1U0Y0V0)
1: Big endian  (Y0U0Y1V0)
*/
  DWORD YUV422_FORMAT:2;/*Indicates the input YUV422 format:
2¡¯b00:  YUYV
2¡¯b01:  YVYU
2¡¯b10:  UYVY
2¡¯b11:  VYUY
*/
  DWORD OUT_YUV422_FORMAT:2;/*Indicates the output YUV422 format, if the output format is YUYV:
2¡¯b00:  YUYV
2¡¯b01:  YVYU
2¡¯b10:  UYVY
2¡¯b11:  VYUY
*/
  DWORD RESERVE0:1;
  DWORD DEST:1;/*Indicates where the result will be output:
0: Output result to memory
1: Output result to layer1 of the LCD controller to display
*/
  DWORD OUT_FORMAT:2;/*00: 16bit RGB565
01: 18bit RGB666
10: 24bit RGB888
11: YUV422, bypasses the YUV2RGB function
*/
  DWORD OUT_ENDIAN_MODE:1;/*Indicates the endian mode of output YUV422 format:
0: Little endian (Y1U0Y0V0)
1: Big endian  (Y0U0Y1V0)
*/
  DWORD CLK_OFF_ENABLE:1;/*Indicates whether the clock should be off when VPP has completed the current frame:
0: VPP clock is on even when the current frame is completed
1: VPP clock is off when the current frame is completed
*/
  DWORD RESERVE1:1;
  DWORD RESERVE2:1;
  DWORD UV_INTERLEAVE_EN:1;/* **READ ONLY**Indicates whether to use hardware to fix bug# 3160
This bit should be set to ¡°0¡±
0: To use software, this is the recommended configuration
*/
  DWORD HW_DI_MODE:2;/*It indicates the HW de-interlace method for interlace video sequence.
00: reserved
01: weave method, EVEN/ODD filed merge to do scaling
10: 3-Median method
11: Vertical Median Ranking Interpolation method
Note:
1.	For intra field method, just use a field as a frame to do scaling.
2.	Weave method is only valid for YUV422 format. For YUV420, the video decoder will do the merge operation, and the sequence is treated as progressive sequence.
*/
  DWORD SEQ_TYPE:2; /*Indicates if the video sequence input to VPP and output from VPP.
00: progressive-in-progressive-out, no de-interlace will be applied
01: progressive-in-interlace-out, no de-interlace will be applied
10: interlace-in-progressive-out, de-interlace will be applied according to HW_DI_MODE settings
11: interlace-in-interlace-out, no de-interlace will be applied
NOTE:
1.	If field drop de-interlace method is used, SW should set SEQ_TYPE=00, and the remained  field is treated as a frame
2.	If SEQ_TYPE=00, source input pictures should be organized as a frame format; Otherwise, source input pictures should be organized as 2-field format
*/
  DWORD TOP_FIELD_FIRST:1; /*Indicates whether top or bottom field is the first field to display.
0: bottom first
1: top first
*/
  DWORD DI_FIELD_BOT:1; /*Indicates which field is the reserved field and which field will be interpolated when 3-Median or Vertical Median Ranking deinterlacing is enabled.
0: top field reserved: The top field is copied to dest frame and the bottom field is interpolated.
1: bot field reserved. The bottom field is copied to dest frame and the top field is interpolated.
*/
  DWORD DOUBLE_FRATE:1; /*Indicates the frame rate should be doubled when SEQ_TYPE=2¡¯b10*/
  DWORD RESERVE3:7;
  DWORD START:1;/*Starts VPP in single mode:
0: Not starting VPP
1: Starting VPP after software sets this bit, hardware will clear this bit automatically
*/
  DWORD SCA_OVER:1;/*This bit is for debugging purposes only:
1¡¯b0: Scaling is not over
1¡¯b1: Scaling is over, but data may not be written out
*/
  DWORD BUSY_STATUS:1;/*1¡¯b1: VPP is running
1¡¯b0: Operation finished
*/
    } ;
  DWORD DW;
} REG_VPP_CTRL;

typedef union
{
    struct
    {
  DWORD YBASE_ADDR  :30; /*Memory address for Y buffer when working in YUV420 mode.
Memory address when working in YUV422 mode. 
When using yuv422 weave interlacing mode, this address will be pointed to the odd field.
This register must be QWORD alignment.*/
  DWORD RESERVE:2;
    } ;
  DWORD DW;
} REG_VPP_YBASE;

typedef union
{
    struct
    {
  DWORD UBASE_ADDR    :30; /*Memory byte address for U buffer when working inYUV420 mode.
When using yuv422weave interlacing mode, this address will be pointed to the even field.
This register must be QWORD alignment.*/
  DWORD RESERVED0   :2;
    } ;
  DWORD DW;
} REG_VPP_UBASE;

typedef union
{
    struct
    {
  DWORD VBASE_ADDR    :30; /*Memory byte address for V buffer when working inYUV420 mode.
This register must be QWORD-aligned.*/
  DWORD RESERVED0   :2;
    } ;
  DWORD DW;
} REG_VPP_VBASE;

typedef union
{
    struct
    {
  DWORD DESBASE_ADDR    :30; /*Memory byte address for the destination buffer.
This register must be a QWORD-aligned address. 
If the output is RGB656, then this address should be 2-byte aligned; if output is RGB666 or RGB888, this address should be 4-byte aligned. For YUV420, this address will be 4-byte aligned.
*/
  DWORD RESERVED0   :2;
    } ;
  DWORD DW;
} REG_VPP_DESBASE;


typedef union
{
    struct
    {
  DWORD SRC_WIDTH    :11; /*The source window¡¯s width in pixels:
Max: 1920 pixels wide
Min: 12 pixels wide
*/
  DWORD RESERVED0   :5;
  DWORD DES_WIDTH   :11;/*The destination window width in pixels:
Max: 1280 pixels wide
*/
  DWORD RESERVED1   :5;
    } ;
  DWORD DW;
} REG_VPP_WIDTH;

typedef union
{
    struct
    {
  DWORD SRC_HEIGHT    :11; /*The source window height
If YUV422weave is used, then SRC_HEIGHT should be the height of the frame.
*/
  DWORD RESERVED0   :5;
  DWORD DES_HEIGHT   :11;/*The destination window height*/
  DWORD RESERVED1   :5;
    } ;
  DWORD DW;
} REG_VPP_HEIGHT;

typedef union
{
    struct
    {
  DWORD Y_STRIDE    :13; /*Line stride of Y buffer specified in bytes when working in YUV420 mode;
Line stride of the entire buffer specified in bytes when working in YUV422 mode;
The y start address of the next line should still be QWORD- aligned.
NOTE:
For frame-organized picture, this field should be the stride between line n and line n+1; for field organized picture, this field should be the stride between top_line(or bot_line) n and top_line(or bot_line) n+1
*/
  DWORD RESERVED0   :3;
  DWORD U_STRIDE   :12;/*Line stride of U buffer specified in bytes when working in YUV420 mode;
The u start address of the next line should still be QWORD-aligned.
NOTE:
For frame-organized picture, this field should be the stride between line n and line n+1; for field organized picture, this field should be the stride between top_line(or bot_line) n and top_line(or bot_line) n+1
*/
  DWORD RESERVED1   :4;
    } ;
  DWORD DW;
} REG_VPP_STRIDE0;

typedef union
{
    struct
    {
  DWORD V_STRIDE    :12; /*Line stride of V buffer specified in bytes when working in YUV420 mode;
The v start address of the next line should still be QWORD- aligned.
NOTE:
For frame-organized picture, this field should be the stride between line n and line n+1; for field organized picture, this field should be the stride between top_line(or bot_line) n and top_line(or bot_line) n+1
*/
  DWORD RESERVED0   :4;
  DWORD DES_STRIDE   :13;/*Line stride of the destination buffer specified in bytes;
The start address of the next destination line should meet the following criteria:
RGB565: 2byte aligned;
RGB666/RGB888/YUV422 4byte-aligned.
*/
  DWORD RESERVED1   :3;
    } ;
  DWORD DW;
} REG_VPP_STRIDE1;
 
typedef union
{
     struct
     {
   DWORD COEF00    :15; /*Coefficient for tap0, 
Coefficient is in 2¡¯s complement <2.12>format, for example, 12 fractional data bits (COEFxx[11:0]), 2 integer bits (COEFxx[13:12]) and one sign bit (COEFxx[14]).
*/
   DWORD RESERVED0   :1;
   DWORD COEF01   :15; /*Coefficient for tap1*/
   DWORD RESERVED1   :1;
     } ;
   DWORD DW;
} REG_VPP_HSCA_COEF;

typedef union
{
     struct
     {
   DWORD COEF00    :15; /*Coefficient for tap0, 
Coefficient is in 2¡¯s complement <2.12>format, i.e., 12 fractional data bits (COEFxx[11:0]), 2 integer bits (COEFxx[13:12]) and one sign bit (COEFxx[14]).
*/
   DWORD RESERVED0   :1;
   DWORD COEF01   :15; /*Coefficient for tap1*/
   DWORD RESERVED1   :1;
     } ;
   DWORD DW;
} REG_VPP_VSCA_COEF;

typedef union
{
     struct
     {
   DWORD C3    :10; /*V coefficient for R*/
   DWORD C2   :10; /*U coefficient for R*/
   DWORD C1   :10;  /*Y coefficient for R*/
   DWORD RESERVED   :2;
     } ;
   DWORD DW;
} REG_VPP_RCOEF;

typedef union
{
     struct
     {
   DWORD C3    :10; /*V coefficient for G*/
   DWORD C2   :10; /*U coefficient for G*/
   DWORD C1   :10;  /*Y coefficient for G*/
   DWORD RESERVED   :2;
     } ;
   DWORD DW;
} REG_VPP_GCOEF;

typedef union
{
     struct
     {
   DWORD C3    :10; /*V coefficient for B*/
   DWORD C2   :10; /*U coefficient for B*/
   DWORD C1   :10;  /*Y coefficient for B*/
   DWORD RESERVED   :2;
     } ;
   DWORD DW;
} REG_VPP_BCOEF;

typedef union
{
     struct
     {
   DWORD OFFSET1    :24; /*Offset coefficient for R*/
   DWORD RESERVED1   :8;
     } ;
   DWORD DW;
} REG_VPP_OFFSET1;

typedef union
{
     struct
     {
   DWORD OFFSET2    :24; /*Offset coefficient for G*/
   DWORD RESERVED   :8;
     } ;
   DWORD DW;
} REG_VPP_OFFSET2;

typedef union
{
     struct
     {
   DWORD OFFSET3    :24; /*Offset coefficient for B*/
   DWORD RESERVED   :8;
     } ;
   DWORD DW;
} REG_VPP_OFFSET3;

typedef union
{
     struct
     {
   DWORD INT_SINGLE_MASK    :1; /*Interrupt enable of single mode*/
   DWORD INT_CON_MASK   :1; /*Interrupt enable of continuous mode*/
   DWORD INT_AB_MASK    :1; /*Interrupt enable of abnormal condition*/
   DWORD RESERVE   :29;
     } ;
   DWORD DW;
} REG_VPP_INT_MASK;

typedef union
{
     struct
     {
   DWORD INT_SINGLE_STATUS    :1; /*This bit will be set if the frame is completed in single mode; writing this bit will clear this interrupt*/
   DWORD INT_CON_STATUS   :1; /*This bit will be set if scaling is completed in continuous mode; writing this bit will clear this interrupt*/
   DWORD INT_AB_STATUS    :1; /*This bit will be set if either the current frame is not completed while a new start has been set in single mode, or the current frame is not completed while a new VSYNC from the LCD controller comes in continuous mode. Writing this bit will clear this interrupt*/
   DWORD RESERVE   :29;
     } ;
   DWORD DW;
} REG_VPP_INT_STATUS;

typedef union
{
     struct
     {
   DWORD FIFO_FULL_THRESH    :4; /*The full level for the VPP output buffer. This parameter should be set to no less than 8*/
   DWORD UVUV_MODE           :1; /* **READ ONLY** UV format for semi-planar YUV420 format, 1: UVUV, 0:VUVU*/
   DWORD RESERVE   :27;
     } ;
   DWORD DW;
} REG_VPP_FULL_THRESH;

typedef union
{
     struct
     {
   DWORD uC :13; /*uC parameters for hue & saturation operation. It is a 2¡¯s complement value, 1 bit sign value and 12 bit data value*/
   DWORD RESERVE0 :3; 
   DWORD vC :13; /*vC parameters for hue & saturation operation. It is a 2¡¯s complement value, 1 bit sign value and 12 bit data value*/
   DWORD RESERVE1 :3;
     } ;
   DWORD DW;
} REG_VPP_COLOR_HS_CTRL;

typedef union
{
     struct
     {
  DWORD Brightness	:9; /*Brightness parameters for brightness & contrast operation. It is a 2¡¯s complement value, 1 bit sign value and 8 bit data value*/
  DWORD RESERVE0 :7; 
  DWORD Contrast :9; /*Contrast parameters for brightness & contrast operation. It is an unsigned vaule.*/
  DWORD RESERVE1 :7;
     } ;
   DWORD DW;
} REG_VPP_COLOR_BC_CTRL;


typedef union
{
     struct
     {
  DWORD YBASE_ADDR_BOT :30; /*Memory address for Y buffer when working in YUV420 mode.
Memory address when working in YUV422 mode. 
For frame-organized pictures, this address has no meanings.
For field-organized pictures, this address points to the bottom field Y start address
This register must be QWORD alignment.
*/
  DWORD RESERVE   :2;
     } ;
   DWORD DW;
} REG_VPP_YBASE_BOT;

typedef union
{
     struct
     {
  DWORD UBASE_ADDR_BOT :30; /*Memory address for U buffer when working in YUV420 mode.
Memory address when working in YUV422 mode. 
For frame-organized pictures, this address has no meanings.
For field-organized pictures, this address points to the bottom field Y start address
This register must be QWORD alignment.
*/
  DWORD RESERVE   :2;
     } ;
   DWORD DW;
} REG_VPP_UBASE_BOT;
typedef union
{
     struct
     {
  DWORD VBASE_ADDR_BOT :30; /*Memory address for V buffer when working in YUV420 mode.
Memory address when working in YUV422 mode. 
For frame-organized pictures, this address has no meanings.
For field-organized pictures, this address points to the bottom field Y start address
This register must be QWORD alignment.
*/
  DWORD RESERVE   :2;
     } ;
   DWORD DW;
} REG_VPP_VBASE_BOT;

typedef union
{
     struct
     {
  DWORD DSTBASE_ADDR_BOT :30; /*Memory address for destination buffer when working in YUV420 mode.
Memory address when working in YUV422 mode. 
For frame-organized pictures, this address has no meanings.
For field-organized pictures, this address points to the bottom field Y start address
This register must be QWORD alignment.
*/
  DWORD RESERVE   :2;
     } ;
   DWORD DW;
} REG_VPP_DSTBASE_BOT;

#endif  // __A7_VPP_H__

