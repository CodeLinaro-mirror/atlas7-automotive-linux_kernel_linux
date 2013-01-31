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
@Date           10 October 2008

@Platform       Generic

@Description    CSP Common Interface for LCD controller
                Structure or function called from other modules to CSP_CMN
****************************************************************************/

#ifndef CSP_CMN_LCD_H
#define CSP_CMN_LCD_H

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

typedef enum _LCD_LAYER_
{
    LCD_PRIMARY = 0,
    LCD_OVERLAY_1 = 1,
    LCD_OVERLAY_2 = 2,
    LCD_OVERLAY_3 = 3,
    LCD_CURSOR = 6,     
    LCD_LAYER_UNKNOWN = 0xffffffff,
} LCD_LAYER;

typedef enum _LCD_CURSOR_MODE_
{
    LCD_CURSOR_MODE_32x32x2_2_T   = 0,
    LCD_CURSOR_MODE_32x32x2_4     = 1,
    LCD_CURSOR_MODE_32x32x2_3_T   = 2,
    LCD_CURSOR_MODE_64x64x2_2_T   = 4,
    LCD_CURSOR_MODE_64x64x2_4     = 5,
    LCD_CURSOR_MODE_64x64x2_3_T   = 6,
    
}LCD_CURSOR_MODE;

typedef enum _LCD_PIXELFORMAT_
{
    LCD_PIXELFORMAT_UNKNOWN = 0,

    /*
      RGB format goes here
    */
    LCD_PIXELFORMAT_1BPP = 1,
    LCD_PIXELFORMAT_2BPP = 2,
    LCD_PIXELFORMAT_4BPP = 3,
    LCD_PIXELFORMAT_8BPP = 4,

    LCD_PIXELFORMAT_565 = 5,
    LCD_PIXELFORMAT_5551 = 6,
    LCD_PIXELFORMAT_4444 = 7,
    LCD_PIXELFORMAT_5550 = 8,
    LCD_PIXELFORMAT_BGRX_8880 = 9,
    LCD_PIXELFORMAT_8888 = 10,

    LCD_PIXELFORMAT_556 = 11,
    LCD_PIXELFORMAT_655 = 12,
    LCD_PIXELFORMAT_RGBX_8880 = 13,           /* R8G8B8 format */
    LCD_PIXELFORMAT_666 = 14,            /* CSR only */

    LCD_PIXELFORMAT_15BPPGENERIC = 15,   /* some generic types */
    LCD_PIXELFORMAT_16BPPGENERIC = 16,
    LCD_PIXELFORMAT_24BPPGENERIC = 17,
    LCD_PIXELFORMAT_32BPPGENERIC = 18,

    /*
      FOURCC format goes here
    */
    LCD_PIXELFORMAT_UYVY = 19,
    LCD_PIXELFORMAT_UYNV = 20,
    LCD_PIXELFORMAT_YUY2 = 21,
    LCD_PIXELFORMAT_YUYV = 22,
    LCD_PIXELFORMAT_YUNV = 23,
    LCD_PIXELFORMAT_YVYU = 24,
    LCD_PIXELFORMAT_VYUY = 25,
        
    LCD_PIXELFORMAT_IMC2 = 26,           /* 4:2:0 planar YUV formats */
    LCD_PIXELFORMAT_YV12 = 27,
    LCD_PIXELFORMAT_I420 = 28,
        
    LCD_PIXELFORMAT_IMC1 = 29,
    LCD_PIXELFORMAT_IMC3 = 30,
    LCD_PIXELFORMAT_IMC4 = 31,
    LCD_PIXELFORMAT_NV12 = 32,
    LCD_PIXELFORMAT_NV21 = 33,
    LCD_PIXELFORMAT_UYVI = 34,
    LCD_PIXELFORMAT_VLVQ = 35,

    LCD_PIXELFORMAT_CUSTOMFORMAT = 0X1000
}LCD_PIXELFORMAT;

typedef enum
{
    LCD_OUT_8_BIT_RBGRBG = 0,
    LCD_OUT_8_BIT_YUV422 = 1,
    LCD_OUT_16BIT_YUV422 = 2,
    LCD_OUT_18BIT_RBG666 = 3,
    LCD_OUT_24BIT_RBG888 = 4
}LCD_OUT_FORMAT;

typedef enum _LCD_CHIP_ID_
{
    LCD_CHIP_V1 = 1,
    LCD_CHIP_V2 = 2,
    LCD_CHIP_ROM = 3,
} LCD_CHIP_ID;

typedef struct _LCD_WAITFORVBLANK_DATA_
{
    BOOL bBlockBegin;               /* IN: Returns when the vertical-blank interval begins */
} LCD_WAITFORVBLANK_DATA;

typedef struct _LCD_GETSCANLINE_DATA_
{
    UINT32 *pScanLine;              /* OUT: line number */
} LCD_GETSCANLINE_DATA;

typedef struct _LCD_GETMODE_DATA_
{
    LCD_PIXELFORMAT  eFormat;       /* OUT: pixel format type */
    UINT32  ui32ByteStride;         /* OUT: byte stride */
    UINT32  ui32Width;              /* OUT: width */
    UINT32  ui32Height;             /* OUT: height */
    UINT32  ui32RefreshHZ;          /* OUT: refresh rate of the display */
} LCD_GETMODE_DATA;

typedef struct _LCD_GETVIDMEM_DATA_
{
    UINT32  ui32Size;             /* OUT: reserved size for display */
    UINT32  ui32PrimarySize;       /* OUT: primary framebuffer size */
    UINT32  ui32PBase;            /* OUT: physical base address of display */    
    UINT32  ui32VBase;            /* OUT: virtual base address of display */    
} LCD_GETVIDMEM_DATA;

typedef struct _LCD_CURSOR_SHAPE
{
    UINT16  ui16Width;              /* IN: width */
    UINT16  ui16Height;             /* IN: height */
    INT16  i16XHot;                 /* IN: x coord of hot spot */
    INT16  i16YHot;                 /* IN: y coord of hot spot */
    
    VOID*  pvMask;                  /* IN: and/xor cpu virtual address */
    INT16  i16MaskByteStride;       /* IN: and/xor stride */
    
    VOID*  pvColor;                 /* IN: color surface cpu virtual address */
    INT16  i16ColorByteStride;      /* IN: color surface stride*/
    LCD_PIXELFORMAT  eLcdFormat;    /* IN: color surface format */
} LCD_CURSOR_SHAPE;

typedef struct _LCD_CURSOR_INFO_
{
    INT16 i16XPos;                  /* IN: X position */
    INT16 i16YPos;                  /* IN: Y position */
    
    LCD_CURSOR_SHAPE sCursorShape;  /* IN: cursor shape information */
    UINT32 ui32Rotation;            /* IN: rotation mode (0,90,180,270) */

} LCD_CURSOR_INFO;


typedef struct _LCD_SETPARAMS_DATA_
{
    LCD_PIXELFORMAT eLcdFormat;     /* IN/OUT: surface format */
    LCD_LAYER eLayer;               /* IN/OUT: layer index*/
    RECT sRectSrc;                  /* IN: source rect offset */
    RECT sRectDst;                  /* IN: destination rect offset */
    INT32 i32SurfWidth;             /* IN/OUT: surface width/stride */
    INT32 i32SurfHeight;            /* IN/OUT: surface height */

    BOOL bCKeyOn;                   /* IN: if color key enable */
    UINT32 ui32CKHigh;              /* IN: high color key */    
    UINT32 ui32CKLow;               /* IN: low color key */
    UINT32 ui32Base;                /* IN: physical base address of surface */
    BOOL bGlobalAlpha;               /* IN: if global alpha */
    UINT8 ui8Alpha;               /* IN: alpha value */
    BOOL bCKeyDstOn;
    UINT32 ui32CKDstHigh;              /* IN: high color key */    
    UINT32 ui32CKDstLow;               /* IN: low color key */
    BOOL bSourceAlpha;
    BOOL bPremultiAlpha;
    
} LCD_SETPARAMS_DATA;

typedef struct _LCD_ALLOCOVERLAY_DATA_
{
    LCD_PIXELFORMAT eLcdFormat;     /* IN/OUT: surface format */
    INT32 i32Width;             /* IN/OUT: surface width */
    INT32 i32Height;            /* IN/OUT: surface height */
    LCD_LAYER eLayer;               /* IN: layer to be allocated */
	INT32 i32WStridePixel;
	INT32 i32HStridePixel;
	INT32 i32WStrideByte;
	INT32 i32HStrideByte;
} LCD_ALLOCOVERLAY_DATA;

typedef enum _LCD_FLIP_MODE_
{
    LCD_FLIP_FRAME = 0,
    LCD_FLIP_TOP_FIELD = 1,
    LCD_FLIP_BOTTOM_FIELD = 2,
} LCD_FLIP_MODE;

typedef enum _LCD_INTERRUPT_TYPE_
{
    LCD_INTERRUPT_L0_DMA = 0,
	LCD_INTERRUPT_L1_DMA,
	LCD_INTERRUPT_L2_DMA,
	LCD_INTERRUPT_L3_DMA,
	LCD_INTERRUPT_L0_OFLOW = 6,
	LCD_INTERRUPT_L1_OFLOW,
	LCD_INTERRUPT_L2_OFLOW,
	LCD_INTERRUPT_L3_OFLOW,
	LCD_INTERRUPT_L0_UFLOW = 12,
	LCD_INTERRUPT_L1_UFLOW,
	LCD_INTERRUPT_L2_UFLOW,
	LCD_INTERRUPT_L3_UFLOW,
	LCD_INTERRUPT_VSYNC = 18,
	LCD_INTERRUPT_ALL = 0xFFFFFFFF
} LCD_INTERRUPT_TYPE;


#define LCD_COLORCONTROL_BRIGHTNESS     1 
#define LCD_COLORCONTROL_CONTRAST       2
#define LCD_COLORCONTROL_HUE            4
#define LCD_COLORCONTROL_SATURATION     8

typedef struct _LCD_COLORCONTROL
{
    UINT32  ui32Flags;
    INT32   i32Brightness;
    INT32   i32Contrast;
    INT32   i32Hue;
    INT32   i32Saturation;
} LCD_COLORCONTROL;

typedef VOID (*PFN_NOP)(VOID);

#define RGB_SEQ_RGB	0x186
#define RGB_SEQ_BGR	0x924
#define RGB_SEQ_BRG	0x861

typedef struct _LCD_PANEL_INFO_
{
	UINT32 ui32HsyncPeriod;
	UINT32 ui32HsyncWidth;
	UINT32 ui32VsyncPeriod;
	UINT32 ui32VsyncWidth;

	UINT32 ui32HStart;
	UINT32 ui32HEnd;
	UINT32 ui32VStart;
	UINT32 ui32VEnd;
	
	LCD_OUT_FORMAT eOutFormat;
	UINT32 ui32RGBSequence;

	BOOL bPClkPolar;
	BOOL bPClkEdge;
	BOOL bHSyncPolar;
	BOOL bVSyncPolar;
	BOOL bIOMaster;
	UINT32 ui32HSyncDelay;

	UINT32 ui32SysClock;
	UINT32 ui32FreshRate;

	LCD_LAYER eMaxLayer;
	LCD_LAYER eLayer; /* Current primary layer */

	PFN_NOP pfnPrePowerUp;
	PFN_NOP pfnPostPowerUp;
	PFN_NOP pfnPrePowerDown;
	PFN_NOP pfnPostPowerDown;
	PFN_NOP pfnReset;
}LCD_PANEL_INFO;

typedef BOOL (*PFN_CHANGEMODE)(LCD_PANEL_INFO *psPanel);
typedef BOOL (*PFN_INITIALIZE)(VOID *pLcdRegs, 
						VOID *pVppRegs,
						UINT32 ui32PrimBase, 
						UINT32 ui32BitPerPixel,
						LCD_PANEL_INFO *psPanel);
typedef VOID (*PFN_TERMINATE)(VOID);
typedef VOID (*PFN_SLEEP)(VOID);
typedef BOOL (*PFN_WAKEUP)(VOID);
typedef VOID (*PFN_GETSCANLINE)(LCD_GETSCANLINE_DATA *pData);
typedef VOID (*PFN_WAITFORVBLANK)(LCD_WAITFORVBLANK_DATA *pData);
typedef VOID (*PFN_GETMODE)(LCD_GETMODE_DATA *pData);
typedef VOID (*PFN_GETVIDMEM)(LCD_GETVIDMEM_DATA *pData);

typedef LCD_LAYER (*PFN_ALLOCOVERLAY)(LCD_ALLOCOVERLAY_DATA *pData);
typedef VOID (*PFN_FREEOVERLAY)(LCD_LAYER eLayer);

typedef BOOL (*PFN_SETPARAMETERS)(LCD_SETPARAMS_DATA *pData);
typedef VOID (*PFN_GETPARAMETERS)(LCD_SETPARAMS_DATA *pData);
typedef VOID (*PFN_SHOWOVERLAY)(LCD_LAYER eLayer);
typedef VOID (*PFN_HIDEOVERLAY)(LCD_LAYER eLayer);
typedef VOID (*PFN_SETOVERLAYPOS)(LCD_LAYER eLayer, RECT *pSrc, RECT *pDst);
typedef VOID (*PFN_PANDISPLAY)(LCD_LAYER eLayer, INT x, INT y);

typedef VOID (*PFN_SETGLOBALALPHA)(LCD_LAYER eLayer, UINT8 ui8Alpha);
typedef VOID (*PFN_SETALPHAPROPERTY)(LCD_LAYER eLayer, BOOL bPremulti, BOOL bGlobal, BOOL bSource);
typedef VOID (*PFN_SETSRCCKEY)(LCD_LAYER eLayer, BOOL bOn, UINT32 ui32High, UINT32 ui32Low);
typedef VOID (*PFN_SETDSTCKEY)(LCD_LAYER eLayer, BOOL bOn, UINT32 ui32High, UINT32 ui32Low);
typedef VOID (*PFN_SETTOPLAYER)(LCD_LAYER eLayer);
typedef LCD_LAYER (*PFN_GETTOPLAYER)(VOID);

typedef VOID (*PFN_FLIPOVERLAY)(LCD_LAYER eLayer, UINT32 ui32Base, LCD_FLIP_MODE eField);

typedef VOID (*PFN_ENABLEINTERRUPT)(LCD_INTERRUPT_TYPE eType);
typedef VOID (*PFN_DISABLEINTERRUPT)(LCD_INTERRUPT_TYPE eType);
typedef VOID (*PFN_CLEARINTERRUPT)(LCD_INTERRUPT_TYPE eType);
typedef UINT32 (*PFN_ISINTERRUPTED)(LCD_INTERRUPT_TYPE eType);

typedef VOID (*PFN_SETCURSORSHAPE)(UINT32 *pMask, INT iMaskStride, 
	UINT32 *pColor, INT iXHot, INT iYHot, INT iWidth, INT iHeight);
typedef VOID (*PFN_MOVECURSOR)(INT iXPos, INT iYPos);
typedef VOID (*PFN_SETCURSORROTATE)(INT iAngle);

typedef VOID (*PFN_GETGAMMARAMP)(UINT16 *pui16Gamma);
typedef VOID (*PFN_SETGAMMARAMP)(UINT16 *pui16Gamma);
typedef VOID (*PFN_GETCOLORCONTROL)(LCD_LAYER eLayer, LCD_COLORCONTROL *pData);
typedef VOID (*PFN_SETCOLORCONTROL)(LCD_LAYER eLayer, LCD_COLORCONTROL *pData);

typedef VOID* (*PFN_GETVPPTABLE)(VOID);

typedef VOID (*PFN_PRINTREGISTER)(VOID);
typedef VOID (*PFN_RESET)(VOID);
typedef VOID (*PFN_CTRLOUTPUT)(BOOL bTurnOOff);
typedef LCD_CHIP_ID (*PFN_GETCHIPID)(VOID);
typedef VOID (*PFN_SETPIXELCLOCK) (UINT32 ui32PixelClock);
typedef UINT32 (*PFN_GETPIXELCLOCK) (VOID);

typedef struct _LCD_FUNCTIONTABLE_
{
    PFN_INITIALIZE pfnInitialize;
    PFN_TERMINATE pfnTerminate;
    PFN_SLEEP pfnSleep;
    PFN_WAKEUP pfnWakeup;
    PFN_GETSCANLINE pfnGetScanLine;
    PFN_WAITFORVBLANK pfnWaitForVBlank;
    PFN_GETMODE pfnGetMode;
    PFN_GETVIDMEM pfnGetVidMem;

    PFN_ALLOCOVERLAY pfnAllocOverlay;
    PFN_FREEOVERLAY pfnFreeOverlay;

    PFN_SETPARAMETERS pfnSetParameters;
    PFN_GETPARAMETERS pfnGetParameters;
    PFN_SHOWOVERLAY pfnShowOverlay;
    PFN_HIDEOVERLAY pfnHideOverlay;
    PFN_SETOVERLAYPOS pfnSetOverlayPos;
	PFN_PANDISPLAY pfnPanDiaplay;
    PFN_SETGLOBALALPHA pfnSetGlobalAlpha;
    PFN_SETALPHAPROPERTY pfnSetAlphaProperty;
    PFN_SETSRCCKEY pfnSetSrcCKey;
    PFN_SETDSTCKEY pfnSetDstCKey;
    PFN_SETTOPLAYER pfnSetTopLayer;
	PFN_GETTOPLAYER pfnGetTopLayer;
    
    PFN_FLIPOVERLAY pfnFlipOverlay;

    PFN_ENABLEINTERRUPT pfnEnableInterrupt;
    PFN_DISABLEINTERRUPT pfnDisableInterrupt;
    PFN_CLEARINTERRUPT pfnClearInterrupt;
    PFN_ISINTERRUPTED pfnIsInterrupted;

    
    PFN_SETCURSORSHAPE pfnSetCursorShape;
    PFN_MOVECURSOR pfnMoveCursor;
    PFN_SETCURSORROTATE pfnSetCursorRotate;
    
    PFN_GETGAMMARAMP pfnGetGammaRamp;
    PFN_SETGAMMARAMP pfnSetGammaRamp;
    PFN_GETCOLORCONTROL pfnGetColorControl;
    PFN_SETCOLORCONTROL pfnSetColorControl;

    PFN_GETVPPTABLE pfnGetVppTable;

    PFN_PRINTREGISTER pfnPrintRegister;
    PFN_RESET pfnReset;
    PFN_CTRLOUTPUT pfnCtrlOutput;
    PFN_GETCHIPID pfnGetChipID;
	PFN_SETPIXELCLOCK pfnSetPixelClock;
	PFN_GETPIXELCLOCK pfnGetPixelClock;

	PFN_CHANGEMODE pfnChangeMode;
}LCD_FUNCTIONTABLE;


/***************************************************************************
** 
** Declare Fuction
****************************************************************************/

VOID LCD_GetFuncTable(LCD_FUNCTIONTABLE *pData);
VOID LCD_BootUp(VOID* pLcdRegs, 
					UINT32 ui32PrimBase, 
					UINT32 ui32BitPerPixel,
					LCD_PANEL_INFO *psPanel);

/***************************************************************************
** 
** Debug Fuction
****************************************************************************/
#if defined(_WIN32_WCE)
#define LCD_STR(str)				L"LCD: "L##str
#define RAW_STR(str)				L##str
#define LCDDebugMsg					NKDbgPrintfW
#else
#define LCD_STR(str)				str
#define RAW_STR(str)				str
#define LCDDebugMsg(fmt, args...)	printk("LCD: " fmt, ## args)
#endif

#if defined(DEBUG)
#if defined(_WIN32_WCE)
#define LCD_ASSERT(EXPR) \
    do \
    { \
        if (!(EXPR)) {DebugBreak();} \
    } while(0)
#else
#define LCD_ASSERT(EXPR) \
	do \
	{ \
		if (!(EXPR)) { \
			printk(KERN_ERR "LCD: " "Assertion failed! %s, %s, %s, line=%d\n", \
	#EXPR, __FILE__, __func__, __LINE__); \
		} \
	} while(0)
#endif

#define LCD_MSG(X) LCDDebugMsg X
#define LCD_ENTRY(X)
#else
#define LCD_ASSERT(EXPR)
#define LCD_MSG(X) LCDDebugMsg X
#define LCD_ENTRY(X)
#endif


#if defined(__cplusplus)
}
#endif

#endif
