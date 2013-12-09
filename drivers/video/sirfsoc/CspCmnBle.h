/******************************************************************************
 Cambridge Silicon Radio Limited, a CSR plc group company PrimaII BSP/CSP

 Copyright (c) 2010 - 2011  Cambridge Silicon Radio Limited, a CSR plc group
 company.

 All rights reserved.

 This Software is protected by United Kingdom copyright laws and international
 treaties.  You may not reverse engineer, decompile or disassemble this
 Software.

 WARNING:
 This Software contains Cambridge Silicon Radio Limited's confidential and
 proprietary information. UNAUTHORIZED COPYING, USE, MODIFICATION,
 DISTRIBUTION, PUBLICATION, TRANSFER, SALE, RENTAL, REPACKAGING, REASSEMBLING
 OR DISCLOSURE OF THE WHOLE OR ANY PART OF THE SOFTWARE IS PROHIBITED AND MAY
 RESULT IN SERIOUS LEGAL CONSEQUENCES.  Do not copy this Software without
 Cambridge Silicon Radio Limited's express written permission.   Use of any
 portion of the contents of this Software is subject to and restricted by your
 signed written agreement with Cambridge Silicon Radio Limited.
******************************************************************************/
/***************************************************************************
@Date           27 April 2011

@Platform       Generic

@Description    CSP Common Interface for BLE controller
                Structure or function called from other modules to CSP_CMN
****************************************************************************/

#ifndef CSP_CMN_BLE_H
#define CSP_CMN_BLE_H

#include <linux/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************/
/*              Misc Definition Area Begin                               */
#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef INOUT
#define INOUT
#endif

#ifndef VOID
#define VOID  void
#endif

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#define TRUE        1
#define FALSE       0


typedef enum _DRAWCTRL_SHIFT
{
    BLE2D_DRAWCTRL_COLORFILL_SHIFT        = 16,
    BLE2D_DRAWCTRL_ALPHA_SHIFT            = 17,
    BLE2D_DRAWCTRL_FLIP_H_SHIFT           = 19,
    BLE2D_DRAWCTRL_FLIP_V_SHIFT           = 20,
    BLE2D_DRAWCTRL_ROTATION_SHIFT         = 21,
    BLE2D_DRAWCTRL_CLIP_SHIFT             = 23,
    BLE2D_DRAWCTRL_TRANSPARENT_SHIFT      = 24,
    BLE2D_DRAWCTRL_COLORKEY_MODE_SHIFT    = 25,
}BLE2D_DRAWCTRL_SHIFT;

/***************************************************************************
**
** Blt Engine Misc Declare Area
****************************************************************************/

typedef enum _BLE2DOPERATIONMODE
{
    BLE2DMMIOMODE    = 1,
    BLE2DCOMMANDMODE = 2,
}BLE2DOPERATIONMODE;


typedef struct _RINGBUFINO
{
    UINT32   RingBufOffset;
    UINT32   RingBufVirtual;
    UINT32   RingBufSizeInDW;
}RINGBUFINFO;

typedef struct _FENCEBUFINFO
{
    UINT32    FenceBufOffset;
    UINT32    FenceBufVirtual;
    UINT32    FenceBufSize;
}FENCEBUFINFO;


typedef enum
{
    BLE2D_ARGB8888,
    BLE2D_ABGR8888,
    BLE2D_RGB565,
    BLE2D_ARGB1555,  /* this format is not support yet*/
    BLE2D_ARGB4444,  /* this format is not support yet*/
    BLE2D_YUYV,      /* this format is not support yet*/
    BLE2D_YVYU,      /* this format is not support yet*/
    BLE2D_UYVY,      /* this format is not support yet*/
    BLE2D_VYUY,      /* this format is not support yet*/

    /* this format is for LCD support*/
    BLE2D_XRGB8888,
    BLE2D_RGB556,
    BLE2D_RGB655,
    BLE2D_RGB666,
} BLE2DFORMAT;

typedef enum
{
    BLE2D_ALPHA_OP_NON_PREMULTIPLIED = 1,   /* source alpha : Cdst = Csrc*Asrc + Cdst*(1-Asrc) */
    BLE2D_ALPHA_OP_PREMULTIPLIED     = 2    /* premultiplied source alpha : Cdst = Csrc + Cdst*(1-Asrc) */
} BLE2D_ALPHABLENDFUNC;

/* flags for control information of additional blits */
typedef enum
{
    BLE2D_BLIT_DISABLE_ALL                  = 0x00000000,   /* disable all additional controls */
    BLE2D_BLIT_TRANSPARENT_ENABLE           = 0x00000001,   /* enable transparent blt   */
    BLE2D_BLIT_GLOBAL_ALPHA                 = 0x00000002,   /* enable standard global alpha */
    BLE2D_BLIT_PERPIXEL_ALPHA               = 0x00000004,   /* enable per-pixel alpha bleding */
    BLE2D_BLIT_PAT_SURFACE_ENABLE           = 0x00000008,   /* enable pattern surf (disable fill) */
    BLE2D_BLIT_SRC_SURFACE_ENABLE           = 0x00000010,   /* enable source surf  (disable fill) */
    BLE2D_BLIT_ROT_90                       = 0x00000020,   /* apply 90 degree rotation to the blt */
    BLE2D_BLIT_ROT_180                      = 0x00000040,   /* apply 180 degree rotation to the blt */
    BLE2D_BLIT_ROT_270                      = 0x00000080,   /* apply 270 degree rotation to the blt */
    BLE2D_BLIT_FLIP_H                       = 0x00000100,   /* apply mirror in horizontal */
    BLE2D_BLIT_FLIP_V                       = 0x00000200,   /* apply mirror in vertical     */
    BLE2D_BLIT_SRC_COLORKEY                 = 0x00000400,   /* Source color Key  enabled    */
    BLE2D_BLIT_DST_COLORKEY                 = 0x00000800,   /* Destination color Key enabled */
    BLE2D_BLIT_COLOR_FILL                   = 0x00001000,   /* Color fill enabled    */
    BLE2D_BLIT_CLIP_ENABLE                  = 0x00002000,   /* Clipping enabled     */
    BLE2D_BLIT_PATH_BLE2DCORE               = 0x00004000,   /* Blt via dedicated BLE 2D Core */
    BLE2D_BLIT_PATH_EXTERNCORE              = 0x00008000,   /* Blt via extern Core */
    BLE2D_BLIT_PATH_SWBLT                   = 0x00010000,   /* Blt via host software */
} BLE2DBLITFLAGS;

typedef struct _SYNC_OBJECT
{
    ULONG   PhyAddr;
    ULONG   VirAddr;
    ULONG   ulCurrentSyncID;
}SYNC_OBJECT;

/* surface info structure */
typedef struct _BLE2DMEMINFO
{
    ULONG           Reserved1;
    ULONG           Reserved2;
    ULONG           ulOffset;
    ULONG           ulMemSize;
    ULONG           ulTag;
    ULONG           ulDesiredSyncID;
    SYNC_OBJECT     *pSyncObject;
}BLE2DMEMINFO, *PBLE2DMEMINFO;

/* error codes */
typedef enum
{
    BLE2D_OK                            =  0,
    BLE2DERROR_INVALID_PARAMETER        = -1,
    BLE2DERROR_DEVICE_UNAVAILABLE       = -2,
    BLE2DERROR_INVALID_CONTEXT          = -3,
    BLE2DERROR_MEMORY_UNAVAILABLE       = -4,
    BLE2DERROR_DEVICE_NOT_PRESENT       = -5,
    BLE2DERROR_IOCTL_ERROR              = -6,
    BLE2DERROR_GENERIC_ERROR            = -7,
    BLE2DERROR_BLT_NOTCOMPLETE          = -8,
    BLE2DERROR_HW_FEATURE_NOT_SUPPORTED = -9,
    BLE2DERROR_NOT_YET_IMPLEMENTED      = -10,
    BLE2DERROR_MAPPING_FAILED           = -11
}BLE2DERROR;

typedef struct _BLE2DRECTL
{
    LONG  left;
    LONG  top;
    LONG  right;
    LONG  bottom;
}BLE2DRECT,BLE2DRECTL;

typedef struct _BLE2DBLTINFO
{
    ULONG                  ROP3;                  /* rop3 code  */
    ULONG                  FillColor;             /* fill color */
    ULONG                  ColorKey;              /* color key in argb8888 fromat */
    UCHAR                  GlobalAlpha;           /* global alpha blending */
    UCHAR                  AlphaBlendFunc;        /* per-pixel alpha-blending function */
    ULONG                  NumClipRect;
    BLE2DRECTL             *pBleClipRect;
    BLE2DBLITFLAGS         BlitFlags;             /* additional blit control information */

    BLE2DMEMINFO           *pDstMemInfo;          /* destination memory */
    ULONG                  DstStride;             /* signed stride, the number of bytes from pixel 0,0 to 0,1 */
    ULONG                  DstX, DstY;            /* pixel offset from start of dest surface to start of blt rectangle */
    ULONG                  DstSizeX,DstSizeY;     /* blt size */
    BLE2DFORMAT            DstFormat;             /* dest format */
    ULONG                  DstSurfWidth;          /* size of dest surface in pixels */
    ULONG                  DstSurfHeight;         /* size of dest surface in pixels */

    BLE2DMEMINFO           *pSrcMemInfo;          /* source mem, (source fields are also used for patterns) */
    ULONG                  SrcStride;             /* signed stride, the number of bytes from pixel 0,0 to 0,1 */
    LONG                   SrcX, SrcY;            /* pixel offset from start of surface to start of source rectangle */
    ULONG                  SrcSizeX,SrcSizeY;     /* source rectangle size or pattern size in pixels */
    BLE2DFORMAT            SrcFormat;             /* source format */
    ULONG                  SrcSurfWidth;          /* size of source surface in pixels */
    ULONG                  SrcSurfHeight;         /* size of source surface in pixels */
    BOOL                   bSrcExist;

    BLE2DMEMINFO           *pPatMemInfo;          /* pattern memory containing argb8888 color table */
    ULONG                  PatX,PatY;
    ULONG                  PatSizeX,PatSizeY;
    ULONG                  PatOffset;             /* byte offset from start of allocation to start of pattern */
    BOOL                   bPatExist;
    BOOL                   bNeedSyncLast;
}BLE2DBLTINFO, *PBLE2DBLTINFO;

#define BLE2D_MAX_BLIT_CMD_SIZE  0x40

#define RING_BUF_SIZE         (64*1024UL)
#define RING_BUF_ALIGNMENT           0x8
#define RINGBUFFULLGAP               0x4

static ULONG INLINE ConvertRGB565to8888(ULONG color)
{
    return ((((color >> 8) & 0xF8UL) | ((color >> 13)& 0x7UL)) << 16 | // R
            (((color >> 3) & 0xFCUL) | ((color >>  9)& 0x3UL)) <<  8 | // G
            (((color << 3) & 0xF8UL) | ((color >>  2)& 0x7UL)));// B
}

#define BLE2D_PATTERN_WIDTH         0x08
#define BLE2D_PATTERN_HEIGHT        0x08
#define BLE2D_PATTERN_STEP          0x04
#define BLE2D_PATTERN_STRIDE        (BLE2D_PATTERN_STEP   * BLE2D_PATTERN_WIDTH)
#define BLE2D_PATTERN_SIZE          (BLE2D_PATTERN_STRIDE * BLE2D_PATTERN_HEIGHT)

#define MAX_PATTERN_BUF_RESERVED   0x400

typedef struct _BLE2DCONTEXT
{
   BLE2DOPERATIONMODE      ble2dOPMode;
   BLE2DMEMINFO            *pReservedPatSurf[MAX_PATTERN_BUF_RESERVED];
   UINT32                  CurPatBufIndex;
   SYNC_OBJECT             SyncObject;
   BYTE                    *pBleRegs;
   union
   {
   struct
   {
       RINGBUFINFO             RingBuf;
       UINT32                  RingBufSizeLeftInDW;
       UINT32                  *pRingBufWtPtr;
       FENCEBUFINFO            FenceBuf;
   } CmdMode;
   } Mode;
}BLE2DCONTEXT;

#define FENCE_BUF_SIZE       0x1000
#define FENCE_BUF_ALIGNMENT  0x10

#define SYNCOBJECTGAP        0x1000L

/***************************************************************************
**
** Function table Declare Area
****************************************************************************/
typedef struct _INITMEMINFO
{
    UINT32  	RegBase;
    UINT32  	MemOffset;
    UINT32  	MemBase;
    UINT32      MemSize;
}BLE2DINITMEMINFO;

typedef BOOL (*PFN_BLE_INITIALIZE)(INOUT VOID **pBle2DContext, IN VOID *pInitData);
typedef BOOL (*PFN_BLE_TERMINATE)(IN VOID *hContext);
typedef BOOL (*PFN_BLE_CHECKBLTPARAMS)(IN VOID *GPEBltParms);
typedef UINT32 (*PFN_BLE_BITBLT)(IN VOID *hContext, IN VOID *BltInfo);
typedef BLE2DERROR (*PFN_BLE_QUERYBLTSTATUS)(IN VOID *pBle2DContext, IN VOID *pMemInfo, IN BOOL bWait);
typedef VOID (*PFN_BLE_INTERRUPTROUTINE)(IN VOID *hContext);
typedef VOID (*PFN_BLE_WAKEUP)(VOID);
typedef VOID (*PFN_BLE_SLEEP)(VOID);
typedef VOID (*PFN_BLE_ENABLECLOCK)(VOID);
typedef VOID (*PFN_BLE_DISABLECLOCK)(VOID);
typedef VOID (*PFN_BLE_RESET)(VOID);
typedef VOID (*PFN_BLE_PRINTREGISTERS)(VOID);

typedef struct _BLE_FUNCTIONTABLE
{
    PFN_BLE_INITIALIZE       pfnInitialize;
    PFN_BLE_TERMINATE        pfnTerminate;
    PFN_BLE_CHECKBLTPARAMS   pfnCheckBltParams;
    PFN_BLE_BITBLT           pfnBitBlt;
    PFN_BLE_QUERYBLTSTATUS   pfnQueryBltStatus;
    PFN_BLE_INTERRUPTROUTINE pfnInterruptRoutine;
    PFN_BLE_WAKEUP           pfnWakeup;
    PFN_BLE_SLEEP            pfnSleep;
    PFN_BLE_ENABLECLOCK      pfnEnableClock;
    PFN_BLE_DISABLECLOCK     pfnDisableClock;
    PFN_BLE_RESET            pfnReset;

    /*for Debug Purpose */
    PFN_BLE_PRINTREGISTERS   pfnPrintRegisters;
}BLE_FUNCTIONTABLE;

VOID BleSoc_GetFuncTable(INOUT BLE_FUNCTIONTABLE *pData);
#ifdef __cplusplus
}
#endif

#endif
