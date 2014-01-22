/*
 * CSR sirfsoc LCD internal interface
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __CSP_SOC_LCD_INTERNAL_H__
#define __CSP_SOC_LCD_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "CspCmnLcd.h"
#include "LCDV2Regs.h"
#include "CspCmnVpp.h"

/***************************************************************************
** 
** OS Dependent
****************************************************************************/

#if defined(_WIN32_WCE)
#include "macros.h"
#include <drvlib.h>
#include "CspSocLcdPanel.h"
#include "BspLcd.h"
#include "SOC_LCD.h"
		
	VOID __LcdSoc_Reset(VOID);
	VOID __LcdSoc_EnableClock(VOID);
	VOID __LcdSoc_DisableClock(VOID);
#else
#include <linux/string.h>
#include <linux/delay.h>
		
		
		/*Just skip the following function for linux*/
#define LcdBsp_PrePowerDown(args...)
#define LcdBsp_PostPowerDown(args...)
#define LcdBsp_PrePowerUp(args...)
#define LcdBsp_PostPowerUp(args...)
#define LcdBsp_CtrlOutput(args...)
		
#define __LcdSoc_EnableClock(args...)
#define __LcdSoc_DisableClock(args...)
#define __LcdSoc_Reset(args...)
#endif


typedef struct _LCDSOC_CURSOR_STATE_
{
    INT iWidth;
    INT iHeight;
    INT iXHot;
    INT iYHot;
    INT iXPos;
    INT iYPos;
    INT iRotate;
    UINT32 aui32FIFO[256*2]; /*first 256 for normal, second 256 for rotate*/
    BOOL bShow;
} LCDSOC_CURSOR_STATE;

typedef struct _LCDSOC_LAYER_STATE_
{
    BOOL bShow;
    BOOL bInUse;
    BOOL bNeedVpp;

    RECT sRectSrc;                  /* IN: source rectangle */
    RECT sRectDst;                  /* IN: destination rectangle */

    RECT sRectSrcOn;                /* Current HW state: src rect on screen */
    RECT sRectDstOn;                /* Current HW state: dst rect on screen */
    
    UINT32 ui32SurfWidth;
    UINT32 ui32SurfHeight;
    LCD_PIXELFORMAT eLcdFormat;

    BOOL bCKeyOn;                   /* IN: if color key enable */
    UINT32 ui32CKHigh;              /* IN: high color key */    
    UINT32 ui32CKLow;               /* IN: low color key */
    UINT32 ui32Base;                /* IN: physical base address of surface */
    BOOL bGlobalAlpha;              /* IN: if const alpha */
    UINT8 ui8Alpha;                 /* IN: alpha value */
    BOOL bCKeyDstOn;
    UINT32 ui32CKDstHigh;           /* IN: high color key */    
    UINT32 ui32CKDstLow;            /* IN: low color key */
    BOOL bSourceAlpha;
    BOOL bPremultiAlpha;
    BOOL bReplicate;
    UINT32 ui32BaseOn;

    LCD_FLIP_MODE   eFlipMode;

    INT32   i32Brightness;
    INT32   i32Contrast;
    INT32   i32Hue;
    INT32   i32Saturation;
} LCDSOC_LAYER_STATE;

typedef struct _LCDSOC_CONFIG_
{
    /*
    ** Immutable setting from Bootup
    */
    UINT32 ui32FBSize;      /* Size of RAM based Video Memory (should be a multiple of screen Pitch) */

    /*
    ** Screen setting
    */
	LCD_LAYER eTopLayer;
    /*
     * Interrupt state
     */
    UINT32 ui32IntState;

    /*
    ** Layer setting
    */
    LCDSOC_LAYER_STATE sLayerState[E_LAYER_NUM];

    /*
    ** Cursor setting
    */
    LCDSOC_CURSOR_STATE sCursorState;

    /*
    ** VPP handle
    */
    VOID *hVppHandle;

    /*
    ** Gamma Ramp Table
    */
    BOOL   bGammaEnable;
    UINT8  aui8Gamma[256 * 3];
} LCDSOC_CONFIG;

/*
** Variable exported
*/
extern LCD_PANEL_INFO gsPanelInfo;
extern LCDSOC_CONFIG gsLcdConfig;
extern volatile UINT8 *gpui8LcdRegs;


static INLINE UINT32 PIXEL_CLOCK(UINT32 dispFreq, UINT32 HPSync, UINT32 VPSync)
{
    return (dispFreq * (HPSync+1)* (VPSync+1));
}

static INLINE UINT32 REFRESH_RATE(UINT32 PixelClock, UINT32 HPSync, UINT32 VPSync)
{
    return (PixelClock / (HPSync+1) / (VPSync+1));
}

static INLINE UINT32 BYTE_STRIDE(UINT32 screenWidth, UINT32 bpp)
{
    return (((((UINT)bpp) * ((UINT)screenWidth) + 63) >> 6) << 3);
}

static INLINE UINT32 REG_OFFSET(LCD_LAYER eLayer, UINT32 l0RegOffset)
{
    LCD_ASSERT(l0RegOffset >= L0_CTRL);
    LCD_ASSERT(l0RegOffset <L1_CTRL);
    return l0RegOffset + (eLayer<<LCD_LAYER_REG_SHIFT);
}

/*
** Register operation
*/
static INLINE UINT32 ReadLcdRegisterValue(UINT32 ui32Offset)
{
	return (*(volatile UINT32 * const)(gpui8LcdRegs + ui32Offset));
}

static INLINE VOID WriteLcdRegisterValue(UINT32 ui32Offset, UINT32 ui32Value)
{
#ifdef LCD_LOG
    LCD_MSG(("Write LCD 0x%08x=0x%08x \r\n",  ui32Offset, ui32Value));
#endif
	*(volatile UINT32 * const)(gpui8LcdRegs + ui32Offset) = ui32Value;
}

#define LCD_MAX_OVERLAY_WIDTH 2046
#define LCD_MAX_OVERLAY_HEIGHT 2046
#define LCD_DISPLAY_FREQUENCY 60
#define VPP_MODULE_NAME _T("VPP.dll")

//#define VPP_TO_LCD_8880
#ifdef VPP_TO_LCD_8880
#define VPP_TO_LCD_CTRL_BPP          E_LO_CTRL_BPP_RGB888
#define VPP_TO_LCD_PIXELFORMAT       LCD_PIXELFORMAT_BGRX_8880
#define VPP_TO_LCD_BPP               4               
#else
#define VPP_TO_LCD_CTRL_BPP          E_LO_CTRL_BPP_RGB565
#define VPP_TO_LCD_PIXELFORMAT       LCD_PIXELFORMAT_565
#define VPP_TO_LCD_BPP               2               
#endif

/*
** Inline function
*/
static INLINE BOOL __LcdSoc_NeedVpp(LCD_PIXELFORMAT eFormat)
{
    return (eFormat>=LCD_PIXELFORMAT_UYVY);
}


static INLINE BOOL __LcdSoc_IsTVMode(LCD_PANEL_INFO *psPanel)
{
	return (psPanel->eOutFormat == LCD_OUT_8_BIT_YUV422);
}

static INLINE UINT __LcdSoc_DMA_UNIT(BOOL bTVMode, BOOL bOverlay)
{
    if(bTVMode)
    {
        return 32;
    }

    return 128;
}

static INLINE VOID __LcdSoc_ResetLayerFifo(LCD_LAYER eLayer)
{
    REG_L0_CTRL reg_L0_CTRL;
    reg_L0_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL));
    reg_L0_CTRL.FIFO_RESET = 1;
    /* Question: Does it matter that write them together? */
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_L0_CTRL.DW);
    reg_L0_CTRL.FIFO_RESET = 0;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_L0_CTRL.DW);    
}

static INLINE VOID __LcdSoc_ClearLayerConfirmSetting(LCD_LAYER eLayer)
{
    REG_L0_CTRL reg_Lx_CTRL;
    reg_Lx_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL));
    if (reg_Lx_CTRL.CONFIRM)
    {
        reg_Lx_CTRL.CONFIRM = 0;
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_Lx_CTRL.DW);
    }
}

static INLINE VOID __LcdSoc_ConfirmLayerSetting(LCD_LAYER eLayer)
{
    REG_L0_CTRL reg_Lx_CTRL;
    reg_Lx_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL));
    reg_Lx_CTRL.CONFIRM = 1;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_Lx_CTRL.DW);
}

static INLINE VOID __LcdSoc_ConfirmCursorSetting(VOID)
{
    REG_CUR0_CTRL reg_CUR0_CTRL;
    reg_CUR0_CTRL.DW = ReadLcdRegisterValue(CUR0_CTRL);
    reg_CUR0_CTRL.SETTING_VALID = 1;
    WriteLcdRegisterValue(CUR0_CTRL, reg_CUR0_CTRL.DW);
}


/*
** Function declare
*/
ENUM_LO_CTRL_BPP __LcdSoc_EFormatToHwFormat(LCD_PIXELFORMAT eFormat);
LCD_PIXELFORMAT __LcdSoc_HwFormatToEFormat(ENUM_LO_CTRL_BPP hwFormat);
UINT32 __LcdSoc_EFormatToBpp(LCD_PIXELFORMAT eFormat);
VOID __LcdSoc_WaitIdle(LCD_LAYER eLayer, BOOL bWithVpp);
VOID __LcdSoc_DisableLayer(LCD_LAYER eLayer, BOOL bWait);
VOID __LcdSoc_EnableLayer(LCD_LAYER eLayer);
VOID __LcdSoc_SetDma(LCD_LAYER eLayer, RECT *pRectSrc);
UINT32 __LcdSoc_CKValue(LCD_PIXELFORMAT eFormat, BOOL bDuplicate, UINT32 value);
BOOL __LcdSoc_SetParameters(LCD_LAYER eLayer);
VOID __LcdSoc_Lock(LCD_LAYER eLayer, UINT32 ui32Base);
VOID __LcdSoc_CalcSize(RECT *psRectSrcOrig, RECT *psRectDstOrig, BOOL bNeedVpp, RECT *psRectSrc, RECT *psRectDst);
VOID __LcdSoc_SetDstRect(LCD_LAYER eLayer, RECT *psRectDstOn);
VOID __LcdSoc_SetSize(LCD_LAYER eLayer, BOOL bForceUpdate);
VOID __LcdSoc_GetPrimarySize(UINT32 *pui32PrimarySize);
VOID __LcdSoc_GetFBSize(UINT32 *pui32FBSize);
VOID __LcdSoc_ConfigScreen(LCD_PANEL_INFO *psPanel);
VOID __LcdSoc_GetScreenSize(UINT32 *pui32Width, 
								UINT32 *pui32Height, 
								LCD_PANEL_INFO *psPanel);
VOID __LcdSoc_PowerUp(UINT32 ui32PrimBase, 
					LCD_PIXELFORMAT ui32Format,
					LCD_PANEL_INFO *psPanel);
VOID __LcdSoc_PowerDown(VOID);
VOID __LcdSoc_ISRHandler(LCDSOC_CONFIG *pConfig);
VOID __LcdSoc_InstallISR(VOID);
VOID __LcdSoc_Flip(LCD_LAYER eLayer, LCD_FLIP_MODE eField);
VOID __LcdSoc_GenCursorFIFO(UINT32 *pColor, UINT32 *pMask, INT iMaskStride);
VOID __LcdSoc_CalcCursorRegion(INT iXPos, INT iYPos, 
    RECT *pRect, INT *piLeftSkip, INT *piTopSkip);
VOID __LcdSoc_SetCursorRegion(RECT *pRect, INT iLeftSkip, INT iTopSkip);
VOID __LcdSoc_SetCursorShape(VOID);
VOID __LcdSoc_MoveCursor(VOID);
VOID __LcdSoc_SetGlobalAlpha(LCD_LAYER eLayer);
VOID __LcdSoc_SetAlphaProperty(LCD_LAYER eLayer);
VOID __LcdSoc_SetColorKey(LCD_LAYER eLayer);

VOID OSGetPanelInfo(LCD_PANEL_INFO *psPanel);


/**
* Default Replicate conversion setting
**/
#define LCD_DEFAULT_REPLICATE           1
#define LCD_DEFAULT_PREMULTI_ALPHA      1
#define LCD_DEFAULT_REFRESH_RATE        60


#if defined(__cplusplus)
}
#endif

#endif
