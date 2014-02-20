
/*
 * CSR SiRFprima2 VIP library interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef CSP_CMN_VIP_H
#define CSP_CMN_VIP_H

#if defined(__cplusplus)
extern "C" {
#endif


/***************************************************************************
**
** OS Dependent
****************************************************************************/
#if defined(_WIN32_WCE)
#include <windows.h>
#define INLINE __inline
#else

typedef unsigned int	UINT;
typedef signed int		INT;
typedef unsigned char	UINT8;
typedef unsigned char	BYTE;
typedef signed char		INT8;
typedef char			CHAR;
typedef unsigned short	UINT16;
typedef signed short	INT16;
typedef unsigned int	UINT32;
typedef signed int		INT32;
typedef void            VOID;
typedef unsigned int	DWORD;
typedef double			DOUBLE;

typedef	enum _BOOL_
{
	FALSE		= 0,
	TRUE		= 1,
} BOOL;

typedef struct _RECT_
{
    INT32    left;
    INT32    top;
    INT32    right;
    INT32    bottom;
} RECT;

#define ASSERT(EXPR)
#define INLINE inline
#endif


#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef INOUT
#define INOUT
#endif

typedef enum _LCD_PIXELFORMAT_
{
    LCD_PIXELFORMAT_UNKNOWN = 0,

    /* RGB format goes here */
    LCD_PIXELFORMAT_1BPP = 1,
    LCD_PIXELFORMAT_2BPP = 2,
    LCD_PIXELFORMAT_4BPP = 3,
    LCD_PIXELFORMAT_8BPP = 4,

    LCD_PIXELFORMAT_565 = 5,
    LCD_PIXELFORMAT_5551 = 6,
    LCD_PIXELFORMAT_4444 = 7,
    LCD_PIXELFORMAT_5550 = 8,
    LCD_PIXELFORMAT_8880 = 9,
    LCD_PIXELFORMAT_8888 = 10,

    LCD_PIXELFORMAT_556 = 11,
    LCD_PIXELFORMAT_655 = 12,
    LCD_PIXELFORMAT_0888 = 13,           /* R8G8B8 format */
    LCD_PIXELFORMAT_666 = 14,            /* CSR only */

    LCD_PIXELFORMAT_15BPPGENERIC = 15,   /* some generic types */
    LCD_PIXELFORMAT_16BPPGENERIC = 16,
    LCD_PIXELFORMAT_24BPPGENERIC = 17,
    LCD_PIXELFORMAT_32BPPGENERIC = 18,

    /* FOURCC format goes here */
    LCD_PIXELFORMAT_UYVY = 19,
    LCD_PIXELFORMAT_UYNV = 20,
    LCD_PIXELFORMAT_YUY2 = 21,
    LCD_PIXELFORMAT_YUYV = 22,
    LCD_PIXELFORMAT_YUNV = 23,
    LCD_PIXELFORMAT_YVYU = 24,
    LCD_PIXELFORMAT_VYUY = 25,

    LCD_PIXELFORMAT_UYYV = 26,
    LCD_PIXELFORMAT_YUVY = 27,
    LCD_PIXELFORMAT_VYYU = 28,
    LCD_PIXELFORMAT_YVUY = 29,

    LCD_PIXELFORMAT_IMC2 = 30,           /* 4:2:0 planar YUV formats */
    LCD_PIXELFORMAT_YV12 = 31,
    LCD_PIXELFORMAT_I420 = 32,

    LCD_PIXELFORMAT_IMC1 = 33,
    LCD_PIXELFORMAT_IMC3 = 34,
    LCD_PIXELFORMAT_IMC4 = 35,
    LCD_PIXELFORMAT_NV12 = 36,
    LCD_PIXELFORMAT_NV21 = 37,
    LCD_PIXELFORMAT_UYVI = 38,
    LCD_PIXELFORMAT_VLVQ = 39,

    LCD_PIXELFORMAT_CUSTOMFORMAT = 0X1000
}LCD_PIXELFORMAT;


/*************Parameter Definition **************/

/* VIP Control Register Definition */
#define     VIP_CTRL_PXCLK_CTRL        0x00000001
#define     VIP_CTRL_HSYNC_CTRL        0x00000002
#define     VIP_CTRL_VSYNC_CTRL        0x00000004
#define     VIP_CTRL_PIXCLK_INV        0x00000008
#define     VIP_CTRL_HSYNC_INV         0x00000010
#define     VIP_CTRL_VSYNC_INV         0x00000020

#define     VIP_CTRL_OUT_FORMAT_MASK   0x00003000
#define     VIP_CTRL_OUT_RGB888        0x00000000   /* RGB 8:8:8 */
#define     VIP_CTRL_OUT_RGB655        0x00001000   /* RGB 6:5:5 */
#define     VIP_CTRL_OUT_RGB556        0x00002000   /* RGB 5:5:6 */
#define     VIP_CTRL_OUT_RGB565        0x00003000   /* RGB 5:6:5 */

#define     VIP_CTRL_X_SCALE_MASK      0x00030000
#define     VIP_CTRL_NO_X_SCALE        0x00000000   /* no contraction  */
#define     VIP_CTRL_X_SCALE_1_2       0x00010000   /* 1:2 contraction */
#define     VIP_CTRL_X_SCALE_1_4       0x00020000   /* 1:4 contraction */
#define     VIP_CTRL_X_SCALE_1_8       0x00030000   /* 1:8 contraction */

#define     VIP_CTRL_Y_SCALE_MASK      0x000C0000
#define     VIP_CTRL_NO_Y_SCALE        0x00000000   /* no contraction  */
#define     VIP_CTRL_Y_SCALE_1_2       0x00040000   /* 1:2 contraction */
#define     VIP_CTRL_Y_SCALE_1_4       0x00080000   /* 1:4 contraction */
#define     VIP_CTRL_Y_SCALE_1_8       0x000C0000   /* 1:8 contraction */

#ifdef CONFIG_ARCH_ATLAS6
#define     VIP_CTRL_HOR_MIRROR        0x00100000
#define     VIP_CTRL_CAP_FROM_ODD      0x00200000
#define     VIP_CTRL_CAP_FROM_EVEN     0x00400000
#define     VIP_CTRL_PAD_MUX_UPLI      0x00800000
#endif

#define     VIP_CTRL_CCIR656_EN        0x01000000
#define     VIP_CTRL_FID               0x02000000   /* FID */
#define     VIP_CTRL_SINGLE_MODE       0x04000000

/* Interrupt enable and interrupt control register */
#define     VIP_INTMASK_ALL            0x00000007
#define     VIP_INTMASK_SENSOR         0x00000001
#define     VIP_INTMASK_FIFO_OFLOW     0x00000002   /* FIFO overflow  */
#define     VIP_INTMASK_FIFO_UFLOW     0x00000004   /* FIFO underflow */

/* DMA control register */
#define     VIP_DMA_ENDIAN_MASK        0x00000030   /* endian mode */
#define     VIP_DMA_ENDIAN_NOT_CHG     0x00000000   /* endian mode not change */
#define     VIP_DMA_ENDIAN_BXDW        0x00000010   /* byte exchange in dword */
#define     VIP_DMA_ENDIAN_WXDW        0x00000020   /* word exchange in dword */
#define     VIP_DMA_ENDIAN_BXW         0x00000030   /* byte exchange in word  */


/* Value definitions for the bit fields of videoPort registers */

/* Control register, bit field yuv_format */
#define     VIP_CTRL_YUVSEQ_YUYV       0
#define     VIP_CTRL_YUVSEQ_UYYV       1
#define     VIP_CTRL_YUVSEQ_YUVY       2
#define     VIP_CTRL_YUVSEQ_UYVY       3
#define     VIP_CTRL_YUVSEQ_YVYU       4
#define     VIP_CTRL_YUVSEQ_VYYU       5
#define     VIP_CTRL_YUVSEQ_YVUY       6
#define     VIP_CTRL_YUVSEQ_VYUY       7


/* Pixel shift Setting register, bit field yuv_format */
#define     VIP_PIXELSET_DATAPIN_0TO15 0
#define     VIP_PIXELSET_DATAPIN_0TO7  1
#define     VIP_PIXELSET_DATAPIN_1TO8  2
#define     VIP_PIXELSET_DATAPIN_2TO9  3
#define     VIP_PIXELSET_DATAPIN_3TO10 4
#define     VIP_PIXELSET_DATAPIN_4TO11 5
#define     VIP_PIXELSET_DATAPIN_7TO14 6
#define     VIP_PIXELSET_DATAPIN_8TO15 7

/* Interrupt Mask definition */
#define     VIP_INTMASK_ALL            0x00000007
#define     VIP_INTMASK_SENSOR         0x00000001
#define     VIP_INTMASK_FIFO_OFLOW     0x00000002   /* FIFO overflow  */
#define     VIP_INTMASK_FIFO_UFLOW     0x00000004   /* FIFO underflow */

typedef struct _VIP_PARAMS
{
    LCD_PIXELFORMAT eSrcFormat;       /* IN: Format of surface. Refer to FOURCC format of LCD_PIXELFORMAT*/
    UINT32  uiSrcWStride_pixel;       /* IN: Width stride in pixel unit */
    UINT32  uiSrcHStride_pixel;       /* IN: Heigth stride in pixel unit */
    LCD_PIXELFORMAT eDstFormat;       /* IN: Output format*/
    UINT32  ui32DstBase;              /* IN:  Physical address of Dest Surf */
    UINT32  uiDstWStride_pixel;       /* IN: Width stride in pixel unit */
    UINT32  uiDstHStride_pixel;       /* IN: Heigth stride in pixel unit */
    UINT32  PixelBitSelect;
    UINT32  uiFlag;
    BOOL    bFrameBasedInterrupt;
    BOOL    bLoopMode;
    RECT    SrcRect;
    RECT    DstRect;
}VIP_PARAMS;


/******************************Function Table Definition **********************************************/

typedef VOID (*PFN_VIP_INITIALIZE)(IN VOID *pVipRegs, IN VOID *pDMARegs);
typedef VOID (*PFN_VIP_TERMINATE)(VOID);

typedef BOOL (*PFN_VIP_SETPARAMS)(IN VIP_PARAMS* pData);

typedef VOID (*PFN_VIP_SETBASE)(IN UINT32 PhyAddr);/*Set The Base Address for DMA buffer;*/

typedef BOOL (*PFN_VIP_START)(BOOL);
typedef BOOL (*PFN_VIP_STOP)(VOID);

typedef VOID (*PFN_VIP_SLEEP)(VOID);
typedef VOID (*PFN_VIP_WAKEUP)(VOID);
typedef BOOL (*PFN_VIP_RESET)(VOID);

typedef VOID (*PFN_VIP_PRINTREGISTER)(VOID);

typedef BOOL (*PFN_VIP_ISBUSY)(VOID);

typedef VOID (*PFN_VIP_LOCK)(VOID);
typedef VOID (*PFN_VIP_UNLOCK)(VOID);

typedef VOID (*PFN_VIP_RESERVED)(VOID);
typedef VOID (*PFN_VIP_SAVECONFIG)(VOID);
typedef VOID (*PFN_VIP_RESTORECONFIG)(VOID);

typedef VOID (*PFN_VIP_RESETFIFO)(VOID);

typedef VOID (*PFN_VIP_ClearInterrupts)(UINT32);
typedef UINT32 (*PFN_VIP_GetInterrupts)(VOID);

typedef UINT32 (*PFN_VIP_GetFID)(VOID);


typedef struct _VIP_FUNCTIONTABLE_
{
    PFN_VIP_INITIALIZE          pfnInitialize;
    PFN_VIP_TERMINATE           pfnTerminate;

    PFN_VIP_SETPARAMS           pfnSetParams;

    PFN_VIP_SETBASE             pfnSetBase;

    PFN_VIP_START               pfnStart;
    PFN_VIP_STOP                pfnStop;

    PFN_VIP_SLEEP               pfnSleep;
    PFN_VIP_WAKEUP              pfnWakeup;
    PFN_VIP_RESET               pfnReset;

    PFN_VIP_ISBUSY              pfnIsBusy;
    PFN_VIP_SAVECONFIG          pfnSaveConfig;
    PFN_VIP_RESTORECONFIG       pfnRestoreConfig;

    PFN_VIP_RESETFIFO           pfnResetFIFO;

    PFN_VIP_GetInterrupts       pfnGetInterrupts;
    PFN_VIP_ClearInterrupts     pfnClearInterrupts;

    PFN_VIP_GetFID              pfnGetFID;

    /* Internal debug function */
    PFN_VIP_PRINTREGISTER       pfnPrintRegister;

    /* Option */
    PFN_VIP_LOCK                pfnLock;
    PFN_VIP_UNLOCK              pfnUnlock;
    PFN_VIP_RESERVED            pfnReserved;
} VIP_FUNCTIONTABLE;

/***************************************************************************
**
** Declare Fuction
****************************************************************************/

VOID VIP_GetFuncTable(VIP_FUNCTIONTABLE *pTable);
typedef VOID (*PFNVIP_GETFUNCTABLE)(VIP_FUNCTIONTABLE *pTable);

/***************************************************************************
**
** Debug Fuction
****************************************************************************/
#if defined(_WIN32_WCE)
#define VIP_STR(str)				L"VIP: "L##str
#define VIPDebugMsg					NKDbgPrintfW
#else
#define VIP_STR(str)				str
#define VIPDebugMsg(fmt, args...)	printk("VIP: " fmt, ## args)
#endif

#if defined(DEBUG)
#if defined(_WIN32_WCE)
#define VIP_ASSERT(EXPR) \
    do \
    { \
        if (!(EXPR)) {DebugBreak();} \
    } while(0)
#else
#define VIP_ASSERT(EXPR) \
	do \
	{ \
		if (!(EXPR)) { \
			printk(KERN_ERR "VIP: " "Assertion failed! %s, %s, %s, line=%d\n", \
	#EXPR, __FILE__, __func__, __LINE__); \
		} \
	} while(0)
#endif

#define VIP_MSG(X) VIPDebugMsg X
#define VIP_ENTRY(X)
#else
#define VIP_ASSERT(EXPR)
#define VIP_MSG(X) VIPDebugMsg X
#define VIP_ENTRY(X)
#endif

#if defined(__cplusplus)
}
#endif

#endif
