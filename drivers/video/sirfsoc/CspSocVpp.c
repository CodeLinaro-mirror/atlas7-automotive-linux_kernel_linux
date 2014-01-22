/*
 * CSR sirfsoc VPP library
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include "CspSocVppInternal.h"

#if defined(_WIN32_WCE)
#include "macros.h"
#include <drvlib.h>
/*
** Global variable
*/
VPPSOC_CONFIG gsVppConfig = {0};

CRITICAL_SECTION gsCriticalSection;
VOID OSInitializeCriticalSection(VOID)
{
    InitializeCriticalSection(&gsCriticalSection);
}

VOID OSDeleteCriticalSection(VOID)
{
    DeleteCriticalSection(&gsCriticalSection);
}

VOID OSEnterCriticalSection(VOID)
{
    EnterCriticalSection(&gsCriticalSection);
}

VOID OSLeaveCriticalSection(VOID)
{
    LeaveCriticalSection(&gsCriticalSection);
}

VOID __VppSoc_EnableClock(VOID)
{
    if(!gsVppConfig.bUserMode)
    {
        WRITE_BITFIELD(struct clkclkenable, &(v_pClkRegs->clk_clkenable), vpp, 1);
    }
}

VOID __VppSoc_DisableClock(VOID)
{
    if(!gsVppConfig.bUserMode)
    {
        WRITE_BITFIELD(struct clkclkenable, &(v_pClkRegs->clk_clkenable), vpp, 0);
    }
}

VOID __VppSoc_Reset(VOID)
{
    if(!gsVppConfig.bUserMode)
    {
        WRITE_BITFIELD(struct ResetSWRBits, &v_pRstRegs->resetswrReg, vpp, 1);
        usWait(10);
        WRITE_BITFIELD(struct ResetSWRBits, &v_pRstRegs->resetswrReg, vpp, 0);
        usWait(10);
    }
}

INLINE double sinc(double x)
{
    double pi = 2 * asin(1.0);
    x = x * pi;
    if (x != 0.0)
        return(sin(x)/x);
    return(1.0);
}

INLINE double lanczos6(double x)
{
    double I = 3;
    if (x < 0)
        x=(-x);
    if (x < I)
        return(sinc(x)*sinc(x/I));
    return(0.0);
}

INLINE double lanczos4(double x)
{
    double I = 2;
    if (x < 0)
        x=(-x);
    if (x < I)
        return(sinc(x)*sinc(x/I));
    return(0.0);
}

INLINE VOID OSVppWaitms(UINT uCount)
{
    msWait(uCount);
}

VOID *OSMapVppRegs(VOID)
{
    if (CspRegMap(FALSE))
    {
        return (VOID*)v_pVppRegs;
    }
    else
    {
        LCD_MSG(("OSMapVppRegs: CspRegMap fail\n"));
        return NULL;
    }
}

VOID OSUnmapVppRegs(VOID)
{
    CspRegUnMap();
}

#else
#include <linux/string.h>
#include <linux/delay.h>

/*
** Global variable
*/
VPPSOC_CONFIG gsVppConfig = {
    .bInitialized = FALSE,
};

VOID OSInitializeCriticalSection(VOID)
{
}

VOID OSDeleteCriticalSection(VOID)
{
}

VOID OSEnterCriticalSection(VOID)
{
}

VOID OSLeaveCriticalSection(VOID)
{
}

VOID __VppSoc_EnableClock(VOID)
{
}

VOID __VppSoc_DisableClock(VOID)
{
}

VOID __VppSoc_Reset(VOID)
{
}

INLINE VOID OSVppWaitms(UINT uCount)
{
    msleep(uCount);
}

VOID *OSMapVppRegs(VOID)
{
    return NULL;
}

VOID OSUnmapVppRegs(VOID)
{
}

#endif

static const UINT32 tap_filter_coeff[] = 
{
    0x00000000,  
    0x00001000, 
    0x00000000,
    0x7f3f002f,  
    0x00e50fe3, 
    0x00017fc6,
    0x7ea40053,  
    0x01ee0f8f, 
    0x00077f83,
    0x7e2f006c,  
    0x03150f05, 
    0x00117f39,
    0x7ddf007b,  
    0x04560e48, 
    0x001e7eea,
    0x7db10080,  
    0x05aa0d5d, 
    0x002e7e9a,
    0x7da2007c,  
    0x07090c49, 
    0x00407e4e,
    0x7db00072,  
    0x086b0b15, 
    0x00527e0b,
    0x7dd40064,  
    0x09c809c8, 
    0x00647dd4,
    0x10000000,  
    0x00000000,
    0x0fdb7f72,  
    0x7ffc00b7,
    0x0f717f0b,  
    0x7fef0195,
    0x0ec77eca,  
    0x7fd80298,
    0x0de57ea9,  
    0x7fb803ba,
    0x0cd67ea4,  
    0x7f9004f6,
    0x0ba47eb5,  
    0x7f620645,
    0x0a597ed5,  
    0x7f3107a1,
    0x09007f00,  
    0x7f000900,
};

static const UINT32 rgb_yuv_coeff[] =
{
    0x199, 0x0, 0x12A,  /* V, U, Y for R */
    0xD0, 0x64, 0x12A,  /* V, U, Y for G */
    0x0, 0x204, 0x12A,  /* V, U, Y for B */
};

static const UINT32 rgb_offsets[] = 
{
    0xdf20,
    0x8760,
    0x114a0,
};

/*
** Internal function
*/

INLINE ENUM_VPP_HW_DI_MODE convertDi2HWDi(VPP_DI_MODE eDiMode)
{
    /* Values of 2 mode equal. So return directly */
    return (ENUM_VPP_HW_DI_MODE)eDiMode;
}


VOID __VppSoc_SetColorCtrl(VOID)
{
    REG_VPP_COLOR_BC_CTRL reg_VPP_COLOR_BC_CTRL;    
    REG_VPP_COLOR_HS_CTRL reg_VPP_COLOR_HS_CTRL;
    VPP_COLORCTRL_DATA *pClrCtrl = &gsVppConfig.sClrCtrl;

    reg_VPP_COLOR_BC_CTRL.DW = 0;
    reg_VPP_COLOR_BC_CTRL.Brightness = pClrCtrl->i16Bright;
    reg_VPP_COLOR_BC_CTRL.Contrast = pClrCtrl->i16Contrast;
    reg_VPP_COLOR_HS_CTRL.DW = 0;
    reg_VPP_COLOR_HS_CTRL.uC = pClrCtrl->i16UC;
    reg_VPP_COLOR_HS_CTRL.vC = pClrCtrl->i16VC;    
    WriteVppRegisterValue(VPP_COLOR_BC_CTRL, reg_VPP_COLOR_BC_CTRL.DW);
    WriteVppRegisterValue(VPP_COLOR_HS_CTRL, reg_VPP_COLOR_HS_CTRL.DW);
}

VOID __VppSoc_Setup(VOID)
{
    REG_VPP_FULL_THRESH reg_VPP_FULL_THRESH;
    UINT32 ui32Offset, ui32Val;
    INT i;

    if(!gsVppConfig.bUserMode)
        __VppSoc_EnableClock();

    reg_VPP_FULL_THRESH.DW = 0;
    reg_VPP_FULL_THRESH.FIFO_FULL_THRESH = 0x8;
    WriteVppRegisterValue(VPP_FULL_THRESH, reg_VPP_FULL_THRESH.DW);

    ui32Offset = VPP_HSCA_COEF00;
    for (i=0; i<(sizeof(tap_filter_coeff)/sizeof(tap_filter_coeff[0])); i++)
    {
        WriteVppRegisterValue(ui32Offset, tap_filter_coeff[i]);
        ui32Offset += 4;
    }

    ui32Offset = VPP_RCOEF;
    for (i=0; i<(sizeof(rgb_yuv_coeff)/sizeof(rgb_yuv_coeff[0])); i=i+3)
    {
        ui32Val = rgb_yuv_coeff[i] | (rgb_yuv_coeff[i+1]<<10) | (rgb_yuv_coeff[i+2]<<20);
        WriteVppRegisterValue(ui32Offset, ui32Val);
        ui32Offset += 4;
    }

    WriteVppRegisterValue(VPP_OFFSET1, rgb_offsets[0]);
    WriteVppRegisterValue(VPP_OFFSET2, rgb_offsets[1]);
    WriteVppRegisterValue(VPP_OFFSET3, rgb_offsets[2]);

    __VppSoc_SetColorCtrl(); 
}

VOID __VppSoc_SetBase(VOID)
{
    VPP_SETPARAMS_DATA *pData = &gsVppConfig.sSurfaceState;
    REG_VPP_YBASE reg_VPP_YBASE;
    REG_VPP_UBASE reg_VPP_UBASE;
    REG_VPP_VBASE reg_VPP_VBASE;
    REG_VPP_DESBASE reg_VPP_DSTBASE;
    UINT uiYOffset_pixel;
    UINT uiUOffset_pixel;
    UINT uiVOffset_pixel;

    REG_VPP_YBASE_BOT reg_VPP_YBASE_BOT;
    REG_VPP_UBASE_BOT reg_VPP_UBASE_BOT;
    REG_VPP_VBASE_BOT reg_VPP_VBASE_BOT;

    reg_VPP_YBASE.DW = 0;
    reg_VPP_UBASE.DW = 0;
    reg_VPP_VBASE.DW = 0;
    reg_VPP_DSTBASE.DW = 0;

    reg_VPP_YBASE_BOT.DW = 0;
    reg_VPP_UBASE_BOT.DW = 0;
    reg_VPP_VBASE_BOT.DW = 0;

    uiYOffset_pixel = pData->uiSrcWStride_pixel*gsVppConfig.sRectSrc.top+gsVppConfig.sRectSrc.left;
    if( pData->eSrcFormat == LCD_PIXELFORMAT_YV12 || pData->eSrcFormat == LCD_PIXELFORMAT_I420 )
    {
        uiUOffset_pixel = (pData->uiSrcWStride_pixel/2)*gsVppConfig.sRectSrc.top/2+(gsVppConfig.sRectSrc.left/2);
        uiVOffset_pixel = uiUOffset_pixel;
    }
    else if( pData->eSrcFormat == LCD_PIXELFORMAT_IMC1 || pData->eSrcFormat == LCD_PIXELFORMAT_IMC3 ||
             pData->eSrcFormat == LCD_PIXELFORMAT_IMC2 || pData->eSrcFormat == LCD_PIXELFORMAT_IMC4 )
    {
        uiUOffset_pixel = (pData->uiSrcWStride_pixel)*gsVppConfig.sRectSrc.top/2+(gsVppConfig.sRectSrc.left/2);
        uiVOffset_pixel = uiUOffset_pixel;
    }
    else if (pData->eSrcFormat == LCD_PIXELFORMAT_NV12 || pData->eSrcFormat == LCD_PIXELFORMAT_NV21)
    {
        uiUOffset_pixel = pData->uiSrcWStride_pixel * gsVppConfig.sRectSrc.top/2 + gsVppConfig.sRectSrc.left;
        uiVOffset_pixel = uiUOffset_pixel;
    }
    else
    {
        uiVOffset_pixel = uiUOffset_pixel = 0;
    }

    switch(pData->eSrcFormat)
    {
        /* TODO: Need to clarify the format layout later */
        case LCD_PIXELFORMAT_YV12:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_VBASE.VBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+uiVOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = pData->ui32SrcBase+ (pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)*5/4 + uiVOffset_pixel;
            break;
        case LCD_PIXELFORMAT_I420:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+uiUOffset_pixel;
            reg_VPP_VBASE.VBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)*5/4+uiVOffset_pixel;
            break;
        case LCD_PIXELFORMAT_IMC1:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_VBASE.VBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+uiVOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)*3/2+uiUOffset_pixel;
            break;
        case LCD_PIXELFORMAT_IMC2:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+uiVOffset_pixel;
            reg_VPP_VBASE.VBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+
                pData->uiSrcWStride_pixel/2+uiUOffset_pixel;
            break;            
        case LCD_PIXELFORMAT_IMC3:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_VBASE.VBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+uiUOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)*3/2+uiVOffset_pixel;
            break;
        case LCD_PIXELFORMAT_IMC4:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+uiUOffset_pixel;
            reg_VPP_VBASE.VBASE_ADDR = pData->ui32SrcBase+(pData->uiSrcWStride_pixel*pData->uiSrcHStride_pixel)+
                pData->uiSrcWStride_pixel/2+uiVOffset_pixel;
            break;
        case LCD_PIXELFORMAT_NV12: 
        case LCD_PIXELFORMAT_NV21: 
            /* NV12, NV21, hw requried UV base right shift 1*/
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+uiYOffset_pixel;
            reg_VPP_UBASE.UBASE_ADDR = (pData->ui32SrcBase+(pData->uiSrcWStride_pixel*((pData->uiSrcHStride_pixel+0x3f)&(~0x3f)))+uiUOffset_pixel) >> 1;
            reg_VPP_VBASE.VBASE_ADDR = reg_VPP_UBASE.UBASE_ADDR;
            break;
        case LCD_PIXELFORMAT_UYVY:
        case LCD_PIXELFORMAT_UYNV:
        case LCD_PIXELFORMAT_YUY2:
        case LCD_PIXELFORMAT_YUYV:
        case LCD_PIXELFORMAT_YUNV:
        case LCD_PIXELFORMAT_YVYU:
        case LCD_PIXELFORMAT_VYUY:
            reg_VPP_YBASE.YBASE_ADDR = pData->ui32SrcBase+(2*uiYOffset_pixel);
            reg_VPP_UBASE.UBASE_ADDR = reg_VPP_VBASE.VBASE_ADDR = reg_VPP_YBASE.YBASE_ADDR;
            break;
        default:
            LCD_ASSERT(0);
            break;
    }

    if (gsVppConfig.sInterlace.bInInterlaced)
    {
        if (gsVppConfig.sInterlace.ui32FieldOffset)
        {
            reg_VPP_YBASE_BOT.YBASE_ADDR_BOT = reg_VPP_YBASE.YBASE_ADDR+gsVppConfig.sInterlace.ui32FieldOffset;
            reg_VPP_VBASE_BOT.VBASE_ADDR_BOT = reg_VPP_VBASE.VBASE_ADDR+gsVppConfig.sInterlace.ui32FieldOffset;
            reg_VPP_UBASE_BOT.UBASE_ADDR_BOT = reg_VPP_UBASE.UBASE_ADDR+gsVppConfig.sInterlace.ui32FieldOffset; 
        }
        else
        {
            switch(pData->eSrcFormat)
            {
            case LCD_PIXELFORMAT_YV12:
            case LCD_PIXELFORMAT_I420:
            case LCD_PIXELFORMAT_NV12:
            case LCD_PIXELFORMAT_NV21:
                reg_VPP_YBASE_BOT.YBASE_ADDR_BOT = reg_VPP_YBASE.YBASE_ADDR+pData->uiSrcWStride_pixel;
                reg_VPP_VBASE_BOT.VBASE_ADDR_BOT = reg_VPP_VBASE.VBASE_ADDR+pData->uiSrcWStride_pixel/2;
                reg_VPP_UBASE_BOT.UBASE_ADDR_BOT = reg_VPP_UBASE.UBASE_ADDR+pData->uiSrcWStride_pixel/2;
                break;
            case LCD_PIXELFORMAT_IMC4:
            case LCD_PIXELFORMAT_IMC3:
            case LCD_PIXELFORMAT_IMC2:
            case LCD_PIXELFORMAT_IMC1:
                reg_VPP_YBASE_BOT.YBASE_ADDR_BOT = reg_VPP_YBASE.YBASE_ADDR+pData->uiSrcWStride_pixel;
                reg_VPP_VBASE_BOT.VBASE_ADDR_BOT = reg_VPP_VBASE.VBASE_ADDR+pData->uiSrcWStride_pixel;
                reg_VPP_UBASE_BOT.UBASE_ADDR_BOT = reg_VPP_UBASE.UBASE_ADDR+pData->uiSrcWStride_pixel;
                break;
            case LCD_PIXELFORMAT_UYVY:
            case LCD_PIXELFORMAT_UYNV:
            case LCD_PIXELFORMAT_YUY2:
            case LCD_PIXELFORMAT_YUYV:
            case LCD_PIXELFORMAT_YUNV:
            case LCD_PIXELFORMAT_YVYU:
            case LCD_PIXELFORMAT_VYUY:
                reg_VPP_YBASE_BOT.YBASE_ADDR_BOT = reg_VPP_YBASE.YBASE_ADDR+(2*pData->uiSrcWStride_pixel);
                    reg_VPP_UBASE_BOT.UBASE_ADDR_BOT = reg_VPP_VBASE_BOT.VBASE_ADDR_BOT = reg_VPP_YBASE_BOT.YBASE_ADDR_BOT;
                break;
            default:
                LCD_ASSERT(0);
                break;                    
            }
        }
    
        if (gsVppConfig.sInterlace.bInputTopFirst)
        {
            WriteVppRegisterValue(VPP_YBASE, reg_VPP_YBASE.DW);
            WriteVppRegisterValue(VPP_UBASE, reg_VPP_UBASE.DW);
            WriteVppRegisterValue(VPP_VBASE, reg_VPP_VBASE.DW);
            WriteVppRegisterValue(VPP_YBASE_BOT, reg_VPP_YBASE_BOT.DW);
            WriteVppRegisterValue(VPP_UBASE_BOT, reg_VPP_UBASE_BOT.DW);
            WriteVppRegisterValue(VPP_VBASE_BOT, reg_VPP_VBASE_BOT.DW);
        }
        else
        {
            WriteVppRegisterValue(VPP_YBASE_BOT, reg_VPP_YBASE.DW);
            WriteVppRegisterValue(VPP_UBASE_BOT, reg_VPP_UBASE.DW);
            WriteVppRegisterValue(VPP_VBASE_BOT, reg_VPP_VBASE.DW);
            WriteVppRegisterValue(VPP_YBASE, reg_VPP_YBASE_BOT.DW);
            WriteVppRegisterValue(VPP_UBASE, reg_VPP_UBASE_BOT.DW);
            WriteVppRegisterValue(VPP_VBASE, reg_VPP_VBASE_BOT.DW);
        }
    }
    else
    {   
        WriteVppRegisterValue(VPP_YBASE, reg_VPP_YBASE.DW);
        WriteVppRegisterValue(VPP_UBASE, reg_VPP_UBASE.DW);
        WriteVppRegisterValue(VPP_VBASE, reg_VPP_VBASE.DW);
    }
    
    if (pData->ui32DstBase)
    {
        UINT    uiBytesPerPixel;

        if ((LCD_PIXELFORMAT_666 == pData->eDstFormat) ||
            (LCD_PIXELFORMAT_RGBX_8880 == pData->eDstFormat) || 
            (LCD_PIXELFORMAT_BGRX_8880 == pData->eDstFormat))
        {
            uiBytesPerPixel = 4;
        }
        else
        {
            uiBytesPerPixel = 2;
        }
        
        uiYOffset_pixel = pData->uiDstWStride_pixel*gsVppConfig.sRectDst.top+gsVppConfig.sRectDst.left;
        reg_VPP_DSTBASE.DESBASE_ADDR = (pData->ui32DstBase + (uiYOffset_pixel * uiBytesPerPixel)) & (~7);
        WriteVppRegisterValue(VPP_DESBASE, reg_VPP_DSTBASE.DW);
        
        if(gsVppConfig.sInterlace.eOutMode == VPP_OUTPUT_INTERLACE)
        {
            WriteVppRegisterValue(VPP_DESBASE_BOT, reg_VPP_DSTBASE.DESBASE_ADDR + (pData->uiDstWStride_pixel * uiBytesPerPixel));
        }
        else if(gsVppConfig.sInterlace.eOutMode == VPP_OUTPUT_P_DOUBLE)
        {
            WriteVppRegisterValue(VPP_DESBASE_BOT, reg_VPP_DSTBASE.DESBASE_ADDR + (pData->uiDstWStride_pixel * pData->uiDstHStride_pixel * uiBytesPerPixel));
        }
        else
        {
            ASSERT(gsVppConfig.sInterlace.eOutMode == VPP_OUTPUT_P_SINGLE);
            // do nothing
        }
    }

    return;
}


BOOL __VppSoc_SetSize(VOID)
{
    UINT32 ui32SrcWidth = gsVppConfig.sRectSrc.right - gsVppConfig.sRectSrc.left;
    UINT32 ui32DstWidth = gsVppConfig.sRectDst.right - gsVppConfig.sRectDst.left;
    UINT32 ui32SrcHeight = gsVppConfig.sRectSrc.bottom - gsVppConfig.sRectSrc.top;
    UINT32 ui32DstHeight = gsVppConfig.sRectDst.bottom - gsVppConfig.sRectDst.top;
    REG_VPP_WIDTH reg_VPP_WIDTH;
    REG_VPP_HEIGHT reg_VPP_HEIGHT;
    
    gsVppConfig.bValid = TRUE;
    reg_VPP_WIDTH.DW = 0;
    reg_VPP_WIDTH.SRC_WIDTH = ui32SrcWidth;
    reg_VPP_WIDTH.DES_WIDTH = ui32DstWidth;
    WriteVppRegisterValue(VPP_WIDTH, reg_VPP_WIDTH.DW);

    reg_VPP_HEIGHT.DW = 0;
    reg_VPP_HEIGHT.SRC_HEIGHT = ui32SrcHeight;
    reg_VPP_HEIGHT.DES_HEIGHT = ui32DstHeight;
    WriteVppRegisterValue(VPP_HEIGHT, reg_VPP_HEIGHT.DW);

    __VppSoc_SetBase();
    return TRUE;
}

BOOL __VppSoc_SetParames(VOID)
{
    REG_VPP_CTRL reg_VPP_CTRL;
    REG_VPP_STRIDE0 reg_VPP_STRIDE0;
    REG_VPP_STRIDE1 reg_VPP_STRIDE1;
    REG_VPP_FULL_THRESH reg_VPP_THRESH;

    VPP_SETPARAMS_DATA *pData = &gsVppConfig.sSurfaceState;
    VPP_INTERLACE_DATA *psInterlace = &gsVppConfig.sInterlace;
    
    reg_VPP_CTRL.DW = 0;
    reg_VPP_STRIDE0.DW = 0;
    reg_VPP_STRIDE1.DW = 0;
    switch (pData->eSrcFormat)
    {    
    /* TODO: Need to clarify the format layout later */
    case LCD_PIXELFORMAT_YV12:
    case LCD_PIXELFORMAT_I420:
    case LCD_PIXELFORMAT_NV12: /* NV12, NV21, hw requried UV stride right shift 1*/
    case LCD_PIXELFORMAT_NV21:
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV420;
        reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel;
        reg_VPP_STRIDE1.V_STRIDE = reg_VPP_STRIDE0.U_STRIDE = pData->uiSrcWStride_pixel/2;
        break;
    case LCD_PIXELFORMAT_IMC4:
    case LCD_PIXELFORMAT_IMC3:
    case LCD_PIXELFORMAT_IMC2:
    case LCD_PIXELFORMAT_IMC1:        
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV420;
        reg_VPP_STRIDE1.V_STRIDE = reg_VPP_STRIDE0.U_STRIDE = reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel;
        break;
    case LCD_PIXELFORMAT_UYVY:
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV422;
        reg_VPP_CTRL.ENDIAN_MODE = E_VPP_ENDIAN_MODE_LITTLE;
        reg_VPP_CTRL.YUV422_FORMAT = E_VPP_YUV422_FORMAT_VYUY; // UYVY is taken as VYUY, maybe this is a vpp hardware bug.
        reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_UYNV:
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV422;
        reg_VPP_CTRL.ENDIAN_MODE = E_VPP_ENDIAN_MODE_LITTLE;
        reg_VPP_CTRL.YUV422_FORMAT = E_VPP_YUV422_FORMAT_UYVY;
        reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_YUY2:
    case LCD_PIXELFORMAT_YUYV:
    case LCD_PIXELFORMAT_YUNV:
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV422;
        reg_VPP_CTRL.ENDIAN_MODE = E_VPP_ENDIAN_MODE_LITTLE;
        reg_VPP_CTRL.YUV422_FORMAT = E_VPP_YUV422_FORMAT_YUYV;
        reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_YVYU:
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV422;
        reg_VPP_CTRL.ENDIAN_MODE = E_VPP_ENDIAN_MODE_LITTLE;
        reg_VPP_CTRL.YUV422_FORMAT = E_VPP_YUV422_FORMAT_YVYU;
        reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_VYUY:
        reg_VPP_CTRL.PIXEL_FORMAT = E_VPP_PIXEL_FORMAT_YUV422;
        reg_VPP_CTRL.ENDIAN_MODE = E_VPP_ENDIAN_MODE_LITTLE;
        reg_VPP_CTRL.YUV422_FORMAT = E_VPP_YUV422_FORMAT_VYUY;
        reg_VPP_STRIDE0.Y_STRIDE = pData->uiSrcWStride_pixel*2;
        break;
    default:
        LCD_ASSERT(0);
        return FALSE;
    }
    if ((LCD_PIXELFORMAT_NV12 == pData->eSrcFormat) ||
        (LCD_PIXELFORMAT_NV21 == pData->eSrcFormat)) 
    {
        reg_VPP_CTRL.UV_INTERLEAVE_EN = 1; /* WRITE ONLY*/
        gsVppConfig.bUVInterleave = TRUE;
    }
    else
    {
        gsVppConfig.bUVInterleave = FALSE;
    }
     /* Attention, this is write only*/
    reg_VPP_THRESH.DW = ReadVppRegisterValue(VPP_FULL_THRESH);    
    reg_VPP_THRESH.UVUV_MODE = (LCD_PIXELFORMAT_NV12 == pData->eSrcFormat) ? 1 : 0;
    WriteVppRegisterValue(VPP_FULL_THRESH, reg_VPP_THRESH.DW);

    if (psInterlace->bInInterlaced)
    {
        if (psInterlace->ui32FieldOffset == 0)
        {
        reg_VPP_STRIDE0.Y_STRIDE *= 2;
        reg_VPP_STRIDE1.V_STRIDE *= 2;
        reg_VPP_STRIDE0.U_STRIDE *= 2;
        }

        if (psInterlace->eOutMode == VPP_OUTPUT_INTERLACE)
        {
            reg_VPP_CTRL.SEQ_TYPE = E_VPP_SEQ_TYPE_IIIO;
            reg_VPP_CTRL.TOP_FIELD_FIRST = (psInterlace->bOutputTopFirst) ? 1 : 0;
            reg_VPP_CTRL.HW_DI_MODE = 0;          
        }
        else if (psInterlace->eOutMode == VPP_OUTPUT_P_DOUBLE)
        {
            reg_VPP_CTRL.DOUBLE_FRATE = 1;
            reg_VPP_CTRL.SEQ_TYPE = E_VPP_SEQ_TYPE_IIPO;
            reg_VPP_CTRL.DI_FIELD_BOT = (psInterlace->bTopDi)?1:0;
            reg_VPP_CTRL.TOP_FIELD_FIRST = (psInterlace->bOutputTopFirst) ? 1 : 0;
            reg_VPP_CTRL.HW_DI_MODE = convertDi2HWDi(psInterlace->eDeintMode);          
        }
        else
        {
            ASSERT(psInterlace->eOutMode == VPP_OUTPUT_P_SINGLE);
            
            reg_VPP_CTRL.SEQ_TYPE = E_VPP_SEQ_TYPE_IIPO;
            reg_VPP_CTRL.DI_FIELD_BOT = (psInterlace->bTopDi)?1:0;
            reg_VPP_CTRL.HW_DI_MODE = convertDi2HWDi(psInterlace->eDeintMode);
        }
    }
    else
    {
        if (psInterlace->eOutMode == VPP_OUTPUT_INTERLACE)
        {
            reg_VPP_CTRL.SEQ_TYPE = E_VPP_SEQ_TYPE_PIIO;
            reg_VPP_CTRL.HW_DI_MODE = 0;
            reg_VPP_CTRL.TOP_FIELD_FIRST = (psInterlace->bOutputTopFirst) ? 1 : 0;
        }
        else
        {
            reg_VPP_CTRL.SEQ_TYPE = E_VPP_SEQ_TYPE_PIPO;
            reg_VPP_CTRL.HW_DI_MODE = 0;
        }
    }

    if (pData->ui32DstBase == 0)
    {
        reg_VPP_CTRL.DEST = E_VPP_DEST_LCD;
    }
    else
    {
        reg_VPP_CTRL.DEST = E_VPP_DEST_MEMORY;
    }
    
    switch (pData->eDstFormat)
    {
    case LCD_PIXELFORMAT_565:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_RGB565;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_666:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_RGB666;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*4;
        break;
    case LCD_PIXELFORMAT_BGRX_8880:
	case LCD_PIXELFORMAT_RGBX_8880:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_RGB888;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*4;
        break;
    case LCD_PIXELFORMAT_YUYV:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_YUV422;
        reg_VPP_CTRL.OUT_YUV422_FORMAT = E_VPP_YUV422_FORMAT_YUYV;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_YVYU:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_YUV422;
        reg_VPP_CTRL.OUT_YUV422_FORMAT = E_VPP_YUV422_FORMAT_YVYU;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_UYVY:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_YUV422;
        reg_VPP_CTRL.OUT_YUV422_FORMAT = E_VPP_YUV422_FORMAT_UYVY;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*2;
        break;
    case LCD_PIXELFORMAT_VYUY:
        reg_VPP_CTRL.OUT_FORMAT = E_VPP_OUT_FORMAT_YUV422;
        reg_VPP_CTRL.OUT_YUV422_FORMAT = E_VPP_YUV422_FORMAT_VYUY;
        reg_VPP_STRIDE1.DES_STRIDE = pData->uiDstWStride_pixel*2;
        break;
    default:
        LCD_ASSERT(0);
        return FALSE;
    }
    WriteVppRegisterValue(VPP_CTRL, reg_VPP_CTRL.DW);
    WriteVppRegisterValue(VPP_STRIDE0, reg_VPP_STRIDE0.DW);
    WriteVppRegisterValue(VPP_STRIDE1, reg_VPP_STRIDE1.DW);

	if (pData->eDstFormat == LCD_PIXELFORMAT_RGBX_8880)
	{
		UINT32 ui32Val;
		ui32Val = rgb_yuv_coeff[0] | (rgb_yuv_coeff[1]<<10) | (rgb_yuv_coeff[2]<<20);
		WriteVppRegisterValue(VPP_BCOEF, ui32Val);
		ui32Val = rgb_yuv_coeff[6] | (rgb_yuv_coeff[7]<<10) | (rgb_yuv_coeff[8]<<20);
		WriteVppRegisterValue(VPP_RCOEF, ui32Val);

		WriteVppRegisterValue(VPP_OFFSET3, rgb_offsets[0]);
		WriteVppRegisterValue(VPP_OFFSET1, rgb_offsets[2]);
	}
	else
	{
		UINT32 ui32Val;
		ui32Val = rgb_yuv_coeff[0] | (rgb_yuv_coeff[1]<<10) | (rgb_yuv_coeff[2]<<20);
		WriteVppRegisterValue(VPP_RCOEF, ui32Val);
		ui32Val = rgb_yuv_coeff[6] | (rgb_yuv_coeff[7]<<10) | (rgb_yuv_coeff[8]<<20);
		WriteVppRegisterValue(VPP_BCOEF, ui32Val);

		WriteVppRegisterValue(VPP_OFFSET1, rgb_offsets[0]);
		WriteVppRegisterValue(VPP_OFFSET3, rgb_offsets[2]);
	}
    return TRUE;    
}


/*
** VPP SOC function
*/
VOID VppSoc_Initialize(VOID *pVppRegs)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (!gsVppConfig.bInitialized)
    {
        memset(&gsVppConfig, 0, sizeof(gsVppConfig));
        gsVppConfig.bInitialized = TRUE;
        OSInitializeCriticalSection();

        if (pVppRegs == NULL)
        {
            if (gsVppConfig.pVppRegs == NULL)
            {
                gsVppConfig.bNeedUnmap = TRUE;
                LCD_MSG(("VPP call CspRegMap..."));
                gsVppConfig.pVppRegs = OSMapVppRegs();
            }
        }
        else
        {
            gsVppConfig.bNeedUnmap = FALSE;
            gsVppConfig.pVppRegs = pVppRegs;
        }
        
        gsVppConfig.bDMAInterruptEnabled = FALSE;

        gsVppConfig.fHScalingRatioLast = 1.0;
        gsVppConfig.fVScalingRatioLast = 1.0;

        gsVppConfig.sClrCtrl.i16UC = 0x100;
        gsVppConfig.sClrCtrl.i16VC = 0;
        gsVppConfig.sClrCtrl.i16Bright = 0;
        gsVppConfig.sClrCtrl.i16Contrast = 0x80;

        gsVppConfig.i16Hue = 0;
        gsVppConfig.i16Saturation = 0x80;
        
        __VppSoc_Setup();
    }
    gsVppConfig.ui32RefCount++;
}

VOID VppSoc_Terminate(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.ui32RefCount--;
    if (gsVppConfig.ui32RefCount==0)
    {
        gsVppConfig.bInitialized = FALSE;
        if (gsVppConfig.bNeedUnmap)
        {
            LCD_MSG(("VPP call CspRegUnMap..."));
            OSUnmapVppRegs();
            gsVppConfig.bNeedUnmap = FALSE;
            gsVppConfig.pVppRegs = NULL;
        }
        OSDeleteCriticalSection();
    }
}

BOOL VppSoc_AllocOverlay(LCD_ALLOCOVERLAY_DATA *pData)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    switch(pData->eLcdFormat)
    {
    case LCD_PIXELFORMAT_NV12:
    case LCD_PIXELFORMAT_NV21:
        /* VXD require wstride&hstride to be 64 pixel aligned */
        pData->i32WStrideByte = pData->i32WStridePixel = ALIGN_SIZE(pData->i32Width, 64);
        pData->i32HStridePixel = ALIGN_SIZE(pData->i32Height, 64);
        pData->i32HStrideByte = ALIGN_SIZE(pData->i32WStrideByte*pData->i32HStridePixel*2, 4096);
        break;
    case LCD_PIXELFORMAT_I420:
        /* MVED require wstride&hstride to be 16 pixel aligned */
        pData->i32WStrideByte = pData->i32WStridePixel = ALIGN_SIZE(pData->i32Width, 16);
        pData->i32HStridePixel = ALIGN_SIZE(pData->i32Height, 16);
        pData->i32HStrideByte = pData->i32WStrideByte*pData->i32HStridePixel*3/2;
        break;
    case LCD_PIXELFORMAT_YV12:
        pData->i32WStridePixel = ALIGN_SIZE(pData->i32Width, 16);
        pData->i32HStridePixel = pData->i32Height;
        pData->i32WStrideByte = pData->i32WStridePixel; // useless
        pData->i32HStrideByte = pData->i32WStrideByte*pData->i32Height; // useless
        break;
    case LCD_PIXELFORMAT_IMC1: // this case has not been tested
    case LCD_PIXELFORMAT_IMC3: // this case has not been tested
    case LCD_PIXELFORMAT_VYUY:
        pData->i32WStridePixel = ALIGN_SIZE(pData->i32Width, 8);
        pData->i32HStridePixel = pData->i32Height;
        pData->i32WStrideByte = pData->i32WStridePixel; // useless
        pData->i32HStrideByte = pData->i32WStrideByte*pData->i32Height; // useless
        break;
    case LCD_PIXELFORMAT_UYVY: // this case has not been tested
    case LCD_PIXELFORMAT_YUY2: // this case has not been tested
    case LCD_PIXELFORMAT_YVYU: // this case has not been tested
    case LCD_PIXELFORMAT_YUYV:
        pData->i32WStrideByte = ALIGN_SIZE(pData->i32Width*2, 8);
        pData->i32HStrideByte = pData->i32WStrideByte*pData->i32Height;
        pData->i32WStridePixel = pData->i32WStrideByte/2;
        pData->i32HStridePixel = pData->i32Height;
        break;
    default:
        LCD_MSG(("Unsupport format (0x%08x) in %s", pData->eLcdFormat, __FUNCTION__));
        LCD_ASSERT(0);
        return FALSE;
    }
    return TRUE;
}

BOOL VppSoc_Lock(BOOL bContinues)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (gsVppConfig.bContinueLock)
    {
        return FALSE;
    }
    else
    {
        OSEnterCriticalSection();
        gsVppConfig.bContinueLock = bContinues;
        return TRUE;
    }
}
VOID VppSoc_Unlock(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.bContinueLock = FALSE;
    OSLeaveCriticalSection();
}


/* Set parameters which will be set only once */
BOOL VppSoc_SetParames(VPP_SETPARAMS_DATA *pData)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.sSurfaceState = *pData;
    __VppSoc_SetParames();
    return TRUE;
}



VOID VppSoc_SetBase(UINT32 ui32Base)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.sSurfaceState.ui32SrcBase = ui32Base;
    __VppSoc_SetBase();
}

BOOL VppSoc_SetSize(RECT *psSrcRect, RECT *psDstRect)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.sRectSrc = (*psSrcRect);
    gsVppConfig.sRectDst = (*psDstRect);
    return __VppSoc_SetSize();
}

VOID VppSoc_Start(BOOL bContinues)
{
    REG_VPP_CTRL reg_VPP_CTRL;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    if (gsVppConfig.bUVInterleave)
    {
        reg_VPP_CTRL.UV_INTERLEAVE_EN = 1;
    }
    reg_VPP_CTRL.START = 1;
    WriteVppRegisterValue(VPP_CTRL, reg_VPP_CTRL.DW);
}

VOID VppSoc_Stop(VOID)
{
    REG_VPP_CTRL reg_VPP_CTRL;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    WriteVppRegisterValue(VPP_CTRL, reg_VPP_CTRL.DW);
}

BOOL VppSoc_IsBusy(VOID)
{
    REG_VPP_CTRL reg_VPP_CTRL;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    return (reg_VPP_CTRL.BUSY_STATUS==1);
}

VOID VppSoc_ClearDMAInterrupt(VOID)
{
    REG_VPP_INT_STATUS reg_VPP_INT_STATUS;
    reg_VPP_INT_STATUS.DW = 0;
    reg_VPP_INT_STATUS.INT_SINGLE_STATUS = 1;
    WriteVppRegisterValue(VPP_INT_STATUS, reg_VPP_INT_STATUS.DW);    
}

VOID VppSoc_EnableDMAInterrupt(VOID)
{
    REG_VPP_INT_MASK reg_VPP_INT_MASK;
    VppSoc_ClearDMAInterrupt();
    gsVppConfig.bDMAInterruptEnabled = TRUE;
    reg_VPP_INT_MASK.DW = ReadVppRegisterValue(VPP_INT_MASK);
    reg_VPP_INT_MASK.INT_SINGLE_MASK = 1;
    WriteVppRegisterValue(VPP_INT_MASK, reg_VPP_INT_MASK.DW);
}

VOID VppSoc_DisableDMAInterrupt(VOID)
{
    REG_VPP_INT_MASK reg_VPP_INT_MASK;
    gsVppConfig.bDMAInterruptEnabled = FALSE;
    reg_VPP_INT_MASK.DW = ReadVppRegisterValue(VPP_INT_MASK);
    reg_VPP_INT_MASK.INT_SINGLE_MASK = 0;
    WriteVppRegisterValue(VPP_INT_MASK, reg_VPP_INT_MASK.DW);
}

BOOL VppSoc_IsDMAInterrupted(VOID)
{
    REG_VPP_INT_STATUS reg_VPP_INT_STATUS;
    reg_VPP_INT_STATUS.DW = ReadVppRegisterValue(VPP_INT_STATUS);
    return (reg_VPP_INT_STATUS.INT_SINGLE_STATUS==1);
}

VOID VppSoc_Sleep(VOID)
{
    UINT times = 0;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    while(VppSoc_IsBusy())
    {
        OSVppWaitms(1);
        times++;
        if (times > 20)
        {
            LCD_MSG(("Error: VPP can't stop\n"));
            break;
        }
    }
    __VppSoc_DisableClock();
}

VOID VppSoc_Wakeup(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    __VppSoc_Setup();

    // Coefs are reset to default table.
    gsVppConfig.fHScalingRatioLast = 1.0;
    gsVppConfig.fVScalingRatioLast = 1.0;

#if 0   
    //
    // Restore registers base on software state, but it seems LCD or
    // blt function will update these parameters.
    //
    __VppSoc_SetParames();
    __VppSoc_SetSize();
#endif
    __VppSoc_SetColorCtrl();
    if (gsVppConfig.bDMAInterruptEnabled)
    {
        VppSoc_EnableDMAInterrupt();
    }
}


VOID VppSoc_UpdateBright(INT iBrightness)
{
    REG_VPP_COLOR_BC_CTRL reg_VPP_COLOR_BC_CTRL;
    VPP_COLORCTRL_DATA *pClrCtrl = &gsVppConfig.sClrCtrl;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));

    pClrCtrl->i16Bright = (INT16)iBrightness;

    reg_VPP_COLOR_BC_CTRL.DW = 0;
    reg_VPP_COLOR_BC_CTRL.Brightness = pClrCtrl->i16Bright;
    reg_VPP_COLOR_BC_CTRL.Contrast = pClrCtrl->i16Contrast;
    WriteVppRegisterValue(VPP_COLOR_BC_CTRL, reg_VPP_COLOR_BC_CTRL.DW);
}

VOID VppSoc_UpdateContrast(INT iContrast)
{
    REG_VPP_COLOR_BC_CTRL reg_VPP_COLOR_BC_CTRL;
    VPP_COLORCTRL_DATA *pClrCtrl = &gsVppConfig.sClrCtrl;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));

    pClrCtrl->i16Contrast = (INT16)(iContrast);

    reg_VPP_COLOR_BC_CTRL.DW = 0;
    reg_VPP_COLOR_BC_CTRL.Brightness = pClrCtrl->i16Bright;
    reg_VPP_COLOR_BC_CTRL.Contrast = pClrCtrl->i16Contrast;
    WriteVppRegisterValue(VPP_COLOR_BC_CTRL, reg_VPP_COLOR_BC_CTRL.DW);
}
VOID VppSoc_SetColorCtrl(VPP_COLORCTRL_DATA *pData)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.sClrCtrl = *pData;
    __VppSoc_SetColorCtrl();
}

VOID VppSoc_SetInterlace(BOOL bInputMode, VPP_OUTPUT_MODE eOutputMode,
    BOOL bOutputTopFirst, BOOL bTopFieldReserved, VPP_DI_MODE eDeinterMode,
    BOOL bInputTopFirst, UINT32 ui32FieldOffset)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.sInterlace.bInInterlaced = bInputMode;
    gsVppConfig.sInterlace.eOutMode = eOutputMode;
    gsVppConfig.sInterlace.bOutputTopFirst = bOutputTopFirst;
    gsVppConfig.sInterlace.bInputTopFirst = bInputTopFirst;
    gsVppConfig.sInterlace.bTopDi = bTopFieldReserved;
    gsVppConfig.sInterlace.eDeintMode = eDeinterMode;
    gsVppConfig.sInterlace.ui32FieldOffset = ui32FieldOffset;
}

VOID VppSoc_PrintRegister(VOID)
{
    LCD_MSG(("VPP registers:\n"));
    LCD_MSG(("VPP_CTRL=0x%08x\r\n",         ReadVppRegisterValue(VPP_CTRL)));
    LCD_MSG(("VPP_YBASE=0x%08x\r\n",        ReadVppRegisterValue(VPP_YBASE)));
    LCD_MSG(("VPP_UBASE=0x%08x\r\n",        ReadVppRegisterValue(VPP_UBASE)));
    LCD_MSG(("VPP_VBASE=0x%08x\r\n",        ReadVppRegisterValue(VPP_VBASE)));
    LCD_MSG(("VPP_DESBASE=0x%08x\r\n",      ReadVppRegisterValue(VPP_DESBASE)));
    LCD_MSG(("VPP_WIDTH =0x%08x\r\n",       ReadVppRegisterValue(VPP_WIDTH)));
    LCD_MSG(("VPP_HEIGHT=0x%08x\r\n",       ReadVppRegisterValue(VPP_HEIGHT)));
    LCD_MSG(("VPP_STRIDE0=0x%08x\r\n",      ReadVppRegisterValue(VPP_STRIDE0)));
    LCD_MSG(("VPP_STRIDE1=0x%08x\r\n",      ReadVppRegisterValue(VPP_STRIDE1)));
    LCD_MSG(("VPP_HSCA_COEF00=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF00)));
    LCD_MSG(("VPP_HSCA_COEF01=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF01)));
    LCD_MSG(("VPP_HSCA_COEF02=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF02)));
    LCD_MSG(("VPP_HSCA_COEF10=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF10)));
    LCD_MSG(("VPP_HSCA_COEF11=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF11)));
    LCD_MSG(("VPP_HSCA_COEF12=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF12)));
    LCD_MSG(("VPP_HSCA_COEF20=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF20)));
    LCD_MSG(("VPP_HSCA_COEF21=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF21)));
    LCD_MSG(("VPP_HSCA_COEF22=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF22)));
    LCD_MSG(("VPP_HSCA_COEF30=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF30)));
    LCD_MSG(("VPP_HSCA_COEF31=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF31)));
    LCD_MSG(("VPP_HSCA_COEF32=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF32)));
    LCD_MSG(("VPP_HSCA_COEF40=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF40)));
    LCD_MSG(("VPP_HSCA_COEF41=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF41)));
    LCD_MSG(("VPP_HSCA_COEF42=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF42)));
    LCD_MSG(("VPP_HSCA_COEF50=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF50)));
    LCD_MSG(("VPP_HSCA_COEF51=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF51)));
    LCD_MSG(("VPP_HSCA_COEF52=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF52)));
    LCD_MSG(("VPP_HSCA_COEF60=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF60)));
    LCD_MSG(("VPP_HSCA_COEF61=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF61)));
    LCD_MSG(("VPP_HSCA_COEF62=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF62)));
    LCD_MSG(("VPP_HSCA_COEF70=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF70)));
    LCD_MSG(("VPP_HSCA_COEF71=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF71)));
    LCD_MSG(("VPP_HSCA_COEF72=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF72)));
    LCD_MSG(("VPP_HSCA_COEF80=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF80)));
    LCD_MSG(("VPP_HSCA_COEF81=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF81)));
    LCD_MSG(("VPP_HSCA_COEF82=0x%08x\r\n",  ReadVppRegisterValue(VPP_HSCA_COEF82)));
    LCD_MSG(("VPP_VSCA_COEF00=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF00)));
    LCD_MSG(("VPP_VSCA_COEF01=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF01)));
    LCD_MSG(("VPP_VSCA_COEF10=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF10)));
    LCD_MSG(("VPP_VSCA_COEF11=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF11)));
    LCD_MSG(("VPP_VSCA_COEF20=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF20)));
    LCD_MSG(("VPP_VSCA_COEF21=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF21)));
    LCD_MSG(("VPP_VSCA_COEF30=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF30)));
    LCD_MSG(("VPP_VSCA_COEF31=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF31)));
    LCD_MSG(("VPP_VSCA_COEF40=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF40)));
    LCD_MSG(("VPP_VSCA_COEF41=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF41)));
    LCD_MSG(("VPP_VSCA_COEF50=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF50)));
    LCD_MSG(("VPP_VSCA_COEF51=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF51)));
    LCD_MSG(("VPP_VSCA_COEF60=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF60)));
    LCD_MSG(("VPP_VSCA_COEF61=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF61)));
    LCD_MSG(("VPP_VSCA_COEF70=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF70)));
    LCD_MSG(("VPP_VSCA_COEF71=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF71)));
    LCD_MSG(("VPP_VSCA_COEF80=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF80)));
    LCD_MSG(("VPP_VSCA_COEF81=0x%08x\r\n",  ReadVppRegisterValue(VPP_VSCA_COEF81)));
    LCD_MSG(("VPP_RCOEF=0x%08x\r\n",        ReadVppRegisterValue(VPP_RCOEF)));
    LCD_MSG(("VPP_GCOEF=0x%08x\r\n",        ReadVppRegisterValue(VPP_GCOEF)));
    LCD_MSG(("VPP_BCOEF=0x%08x\r\n",        ReadVppRegisterValue(VPP_BCOEF)));
    LCD_MSG(("VPP_OFFSET1=0x%08x\r\n",      ReadVppRegisterValue(VPP_OFFSET1)));
    LCD_MSG(("VPP_OFFSET2=0x%08x\r\n",      ReadVppRegisterValue(VPP_OFFSET2)));
    LCD_MSG(("VPP_OFFSET3=0x%08x\r\n",      ReadVppRegisterValue(VPP_OFFSET3)));
    LCD_MSG(("VPP_INT_MASK=0x%08x\r\n",     ReadVppRegisterValue(VPP_INT_MASK)));
    LCD_MSG(("VPP_INT_STATUS=0x%08x\r\n",   ReadVppRegisterValue(VPP_INT_STATUS)));
    LCD_MSG(("VPP_ACC=0x%08x\r\n",          ReadVppRegisterValue(VPP_ACC)));
    LCD_MSG(("VPP_FULL_THRESH=0x%08x\r\n",  ReadVppRegisterValue(VPP_FULL_THRESH)));
    LCD_MSG(("VPP_COLOR_HS_CTRL=0x%08x\r\n",ReadVppRegisterValue(VPP_COLOR_HS_CTRL)));
    LCD_MSG(("VPP_COLOR_BC_CTRL=0x%08x\r\n",ReadVppRegisterValue(VPP_COLOR_BC_CTRL)));
    LCD_MSG(("VPP_YBASE_BOT=0x%08x\r\n",    ReadVppRegisterValue(VPP_YBASE_BOT)));
    LCD_MSG(("VPP_UBASE_BOT=0x%08x\r\n",    ReadVppRegisterValue(VPP_UBASE_BOT)));
    LCD_MSG(("VPP_VBASE_BOT=0x%08x\r\n",    ReadVppRegisterValue(VPP_VBASE_BOT)));
    LCD_MSG(("VPP_DESBASE_BOT=0x%08x\r\n",  ReadVppRegisterValue(VPP_DESBASE_BOT)));
}


/*Copy the data in source buffer to the external buffer*/
VOID VppSoc_GetSourceInfo(UINT *pwidth,UINT *pheight,INT *pformat)
{
    REG_VPP_CTRL  reg_VPP_CTRL;
    REG_VPP_WIDTH reg_VPP_WIDTH;
    REG_VPP_HEIGHT reg_VPP_HEIGHT;
  
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    reg_VPP_WIDTH.DW = ReadVppRegisterValue(VPP_WIDTH);
    reg_VPP_HEIGHT.DW = ReadVppRegisterValue(VPP_HEIGHT);
#if 1
    *pwidth=reg_VPP_WIDTH.SRC_WIDTH;
    *pheight = reg_VPP_HEIGHT.SRC_HEIGHT;
  
    if(reg_VPP_CTRL.PIXEL_FORMAT)//yv12 format
    {
        *pformat=VPP_INFORMAT_YUV420;
    }
    else
    {
        switch(reg_VPP_CTRL.YUV422_FORMAT)
        {
        case 0://YUYV
            if(reg_VPP_CTRL.ENDIAN_MODE)//big endian
                *pformat=VPP_INFORMAT_Y0UY1V;
            else
                *pformat=VPP_INFORMAT_Y1UY0V;
            break;
        case 1://YVYU
            if(reg_VPP_CTRL.ENDIAN_MODE)//big endian
                *pformat=VPP_INFORMAT_Y0VY1U;
            else
                *pformat=VPP_INFORMAT_Y1VY0U;
            break;
        case 2://UYVY
            if(reg_VPP_CTRL.ENDIAN_MODE)//big endian
                *pformat=VPP_INFORMAT_UY0VY1;
            else
                *pformat=VPP_INFORMAT_UY1VY0;
            break;
        case 3://VYUY
            if(reg_VPP_CTRL.ENDIAN_MODE)//big endian
                *pformat=VPP_INFORMAT_VY0UY1;
            else
                *pformat=VPP_INFORMAT_VY1UY0;
            break;
        }
    }
#endif
}

BOOL VppSoc_GetSourceBuffer(UINT8 *pSrc)
{
    UINT8 * p_Yadd;
    UINT j;
    REG_VPP_CTRL  reg_VPP_CTRL;
        
    REG_VPP_STRIDE0 reg_VPP_STRIDE0;
    REG_VPP_STRIDE1 reg_VPP_STRIDE1;
    REG_VPP_WIDTH reg_VPP_WIDTH;
    REG_VPP_HEIGHT reg_VPP_HEIGHT;
  
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    reg_VPP_STRIDE0.DW = ReadVppRegisterValue(VPP_STRIDE0);
    reg_VPP_STRIDE1.DW = ReadVppRegisterValue(VPP_STRIDE1);
    reg_VPP_WIDTH.DW = ReadVppRegisterValue(VPP_WIDTH);
    reg_VPP_HEIGHT.DW = ReadVppRegisterValue(VPP_HEIGHT);
    
    if(reg_VPP_CTRL.DEST)
        return FALSE;
    
    if(reg_VPP_CTRL.PIXEL_FORMAT)//YV12 format
    {
        REG_VPP_YBASE reg_VPP_YBASE;
        REG_VPP_UBASE reg_VPP_UBASE;
        REG_VPP_VBASE reg_VPP_VBASE;
        reg_VPP_YBASE.DW = ReadVppRegisterValue(VPP_YBASE);
        reg_VPP_UBASE.DW = ReadVppRegisterValue(VPP_UBASE);
        reg_VPP_VBASE.DW = ReadVppRegisterValue(VPP_VBASE);
        
        p_Yadd= (UINT8*)(UINT32)reg_VPP_YBASE.YBASE_ADDR;
        
        for(j=0;j<reg_VPP_HEIGHT.SRC_HEIGHT;j++)
        {
            memcpy(pSrc,p_Yadd,reg_VPP_WIDTH.SRC_WIDTH);
            pSrc+=reg_VPP_WIDTH.SRC_WIDTH;
            p_Yadd+=reg_VPP_STRIDE0.Y_STRIDE;
        }
        p_Yadd = (UINT8*)(UINT32)reg_VPP_UBASE.UBASE_ADDR;
        for(j=0;j<reg_VPP_HEIGHT.SRC_HEIGHT/2;j++)
        {
            memcpy(pSrc,p_Yadd,reg_VPP_WIDTH.SRC_WIDTH/2);
            pSrc+=reg_VPP_WIDTH.SRC_WIDTH/2;
            p_Yadd+=reg_VPP_STRIDE0.U_STRIDE;
        }
        
        p_Yadd = (UINT8*)(UINT32)reg_VPP_VBASE.VBASE_ADDR;
        for(j=0;j<reg_VPP_HEIGHT.SRC_HEIGHT/2;j++)
        {
            memcpy(pSrc,p_Yadd,reg_VPP_WIDTH.SRC_WIDTH/2);
            pSrc+=reg_VPP_WIDTH.SRC_WIDTH/2;
            p_Yadd+=reg_VPP_STRIDE1.V_STRIDE;
        }           
    }   
    else
    {
        REG_VPP_YBASE reg_VPP_YBASE;
        reg_VPP_YBASE.DW = ReadVppRegisterValue(VPP_YBASE);
            
        p_Yadd=(UINT8*)(UINT32)reg_VPP_YBASE.YBASE_ADDR;
        
        for(j=0;j<reg_VPP_HEIGHT.SRC_HEIGHT;j++)
        {
            memcpy(pSrc,p_Yadd,reg_VPP_WIDTH.SRC_WIDTH*2);
            pSrc+=reg_VPP_WIDTH.SRC_WIDTH*2;
            p_Yadd+=reg_VPP_STRIDE0.Y_STRIDE;
        }
    }    
    return TRUE;
}

VOID VppSoc_GetDestInfo(UINT *pwidth,UINT *pheight,INT *pformat)
{
    REG_VPP_CTRL  reg_VPP_CTRL;
    REG_VPP_WIDTH reg_VPP_WIDTH;
    REG_VPP_HEIGHT reg_VPP_HEIGHT;
  
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    reg_VPP_WIDTH.DW = ReadVppRegisterValue(VPP_WIDTH);
    reg_VPP_HEIGHT.DW = ReadVppRegisterValue(VPP_HEIGHT);
   
    *pwidth  = reg_VPP_WIDTH.DES_WIDTH;
    *pheight = reg_VPP_HEIGHT.DES_HEIGHT;

#if 1  
    if(reg_VPP_CTRL.OUT_FORMAT==0)//RGB565 format
    {
        *pformat=VPP_OUTFORMAT_RGB565;      
    }     
    else if(reg_VPP_CTRL.OUT_FORMAT==1) //RGB666 format
    {
        *pformat=VPP_OUTFORMAT_RGB666; 
    }
    else if(reg_VPP_CTRL.OUT_FORMAT==2)// RGB8880 format
    {
        *pformat=VPP_OUTFORMAT_RGB888; 
    }
    else if(reg_VPP_CTRL.OUT_FORMAT==3)//YUV422
    {
        switch(reg_VPP_CTRL.OUT_YUV422_FORMAT)
        {
        case 0://YUYV
            if(reg_VPP_CTRL.OUT_ENDIAN_MODE)//big endian
                *pformat=VPP_OUTFORMAT_Y0UY1V;
            else
                *pformat=VPP_OUTFORMAT_Y1UY0V;
            break;
        case 1://YVYU
            if(reg_VPP_CTRL.OUT_ENDIAN_MODE)//big endian
                *pformat=VPP_OUTFORMAT_Y0VY1U;
            else
                *pformat=VPP_OUTFORMAT_Y1VY0U;
            break;
        case 2://UYVY
            if(reg_VPP_CTRL.OUT_ENDIAN_MODE)//big endian
                *pformat=VPP_OUTFORMAT_UY0VY1;
            else
                *pformat=VPP_OUTFORMAT_UY1VY0;
            break;
        case 3://VYUY
            if(reg_VPP_CTRL.OUT_ENDIAN_MODE)//big endian
                *pformat=VPP_OUTFORMAT_VY0UY1;
            else
                *pformat=VPP_OUTFORMAT_VY1UY0;
            break;
        }           
    }
#endif
}

/*Copy the data in dest buffer to the external buffer*/
BOOL VppSoc_GetDestBuffer(UINT8* pDest)
{
    UINT j;
    UINT8 * p_Yadd;
    REG_VPP_CTRL  reg_VPP_CTRL;
    REG_VPP_DESBASE reg_VPP_DSTBASE;
    
    REG_VPP_STRIDE1 reg_VPP_STRIDE1;
    REG_VPP_WIDTH reg_VPP_WIDTH;
    REG_VPP_HEIGHT reg_VPP_HEIGHT;
  
    reg_VPP_CTRL.DW  = ReadVppRegisterValue(VPP_CTRL);
    reg_VPP_STRIDE1.DW = ReadVppRegisterValue(VPP_STRIDE1);
    reg_VPP_WIDTH.DW = ReadVppRegisterValue(VPP_WIDTH);
    reg_VPP_HEIGHT.DW = ReadVppRegisterValue(VPP_HEIGHT);
    
    if(reg_VPP_CTRL.DEST)//pass through mode
        return FALSE;
    
    
    reg_VPP_DSTBASE.DW = ReadVppRegisterValue(VPP_DESBASE);
    
    p_Yadd= (UINT8*)(UINT32)reg_VPP_DSTBASE.DESBASE_ADDR;
        
    for(j=0;j<reg_VPP_HEIGHT.DES_HEIGHT;j++)
    {
        memcpy(pDest,p_Yadd,reg_VPP_WIDTH.DES_WIDTH);
        pDest+=reg_VPP_WIDTH.DES_WIDTH;
        p_Yadd+=reg_VPP_STRIDE1.DES_STRIDE;
    }
    
    return TRUE;
}

#if defined(_WIN32_WCE)
VOID VppSoc_UpdateHue(INT iHue)
{
    REG_VPP_COLOR_HS_CTRL reg_VPP_COLOR_HS_CTRL;
    VPP_COLORCTRL_DATA *pClrCtrl = &gsVppConfig.sClrCtrl;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.i16Hue = (INT16)iHue;
    pClrCtrl->i16UC = (INT16)(cos((double)gsVppConfig.i16Hue / 90.0 * asin(1.0)) * (double)gsVppConfig.i16Saturation * 2.0);
    pClrCtrl->i16VC = (INT16)(sin((double)gsVppConfig.i16Hue / 90.0 * asin(1.0)) * (double)gsVppConfig.i16Saturation * 2.0);

    reg_VPP_COLOR_HS_CTRL.DW = 0;
    reg_VPP_COLOR_HS_CTRL.uC = pClrCtrl->i16UC;
    reg_VPP_COLOR_HS_CTRL.vC = pClrCtrl->i16VC;
    WriteVppRegisterValue(VPP_COLOR_HS_CTRL, reg_VPP_COLOR_HS_CTRL.DW);
}

VOID VppSoc_UpdateSaturation(INT iSaturation)
{
    REG_VPP_COLOR_HS_CTRL reg_VPP_COLOR_HS_CTRL;
    VPP_COLORCTRL_DATA *pClrCtrl = &gsVppConfig.sClrCtrl;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.i16Saturation = (INT16)iSaturation;
    pClrCtrl->i16UC = (INT16)(cos((double)gsVppConfig.i16Hue / 90.0 * asin(1.0)) * (double)gsVppConfig.i16Saturation * 2.0);
    pClrCtrl->i16VC = (INT16)(sin((double)gsVppConfig.i16Hue / 90.0 * asin(1.0)) * (double)gsVppConfig.i16Saturation * 2.0);

    reg_VPP_COLOR_HS_CTRL.DW = 0;
    reg_VPP_COLOR_HS_CTRL.uC = pClrCtrl->i16UC;
    reg_VPP_COLOR_HS_CTRL.vC = pClrCtrl->i16VC;
    WriteVppRegisterValue(VPP_COLOR_HS_CTRL, reg_VPP_COLOR_HS_CTRL.DW);
}

INLINE DWORD double2coeff(DOUBLE x)
{
    INT temp;
    temp = (INT)(x*(DOUBLE)(1<<12));

    LCD_ASSERT(((temp&0xffff8000)==0xffff8000) || ((temp&0xffff8000)==0));
    return temp & 0x7fff;
}

VOID VppSoc_UpdateCoeff(VOID)
{
    UINT32 ui32SrcWidth = gsVppConfig.sRectSrc.right - gsVppConfig.sRectSrc.left;
    UINT32 ui32DstWidth = gsVppConfig.sRectDst.right - gsVppConfig.sRectDst.left;
    UINT32 ui32SrcHeight = gsVppConfig.sRectSrc.bottom - gsVppConfig.sRectSrc.top;
    UINT32 ui32DstHeight = gsVppConfig.sRectDst.bottom - gsVppConfig.sRectDst.top;
    UINT32 ui32Offset;
    
    REG_VPP_HSCA_COEF Lanczos6Coeff;
    REG_VPP_VSCA_COEF Lanczos4Coeff;
    /*Update scaling coeff if the scaling ratio change*/

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    ui32Offset = VPP_HSCA_COEF00;
    if (ui32DstWidth>=ui32SrcWidth)
    {
        if (gsVppConfig.fHScalingRatioLast < 1.0)
        {
            INT i;
            for (i=0; i<27; i++)
            {
                WriteVppRegisterValue(ui32Offset, tap_filter_coeff[i]);
                ui32Offset += 4;
            }
        }
        
        gsVppConfig.fHScalingRatioLast = 1.0;
    }
    else
    {
        if (((gsVppConfig.fHScalingRatioLast - (DOUBLE)ui32DstWidth / ui32SrcWidth) > 10e-9) || 
            ((gsVppConfig.fHScalingRatioLast - (DOUBLE)ui32DstWidth / ui32SrcWidth) < -10e-9))
        {
            INT i;
            DOUBLE acc;

            gsVppConfig.fHScalingRatioLast = (DOUBLE)ui32DstWidth / ui32SrcWidth;

            for (i = 0; i < 9; i++)
            {
#if 1
                DOUBLE frac = (DOUBLE)i / (DOUBLE)(1 << 4); 

                DOUBLE coeff0 = lanczos6((2.0 + frac) * gsVppConfig.fHScalingRatioLast);
                DOUBLE coeff1 = lanczos6((1.0 + frac) * gsVppConfig.fHScalingRatioLast);
                DOUBLE coeff2 = lanczos6((0.0 + frac) * gsVppConfig.fHScalingRatioLast);
                DOUBLE coeff3 = lanczos6((1.0 - frac) * gsVppConfig.fHScalingRatioLast);
                DOUBLE coeff4 = lanczos6((2.0 - frac) * gsVppConfig.fHScalingRatioLast);
                DOUBLE coeff5 = lanczos6((3.0 - frac) * gsVppConfig.fHScalingRatioLast);
                acc = coeff0 + coeff1 + coeff2 + coeff3 + coeff4 + coeff5;

                Lanczos6Coeff.COEF00 = double2coeff(coeff0/acc);
                Lanczos6Coeff.COEF01 = double2coeff(coeff1/acc);
                WriteVppRegisterValue(ui32Offset, Lanczos6Coeff.DW);
                ui32Offset+=4;
                Lanczos6Coeff.COEF00 = double2coeff(coeff2/acc);
                Lanczos6Coeff.COEF01 = double2coeff(coeff3/acc);
                WriteVppRegisterValue(ui32Offset, Lanczos6Coeff.DW);
                ui32Offset+=4;
                Lanczos6Coeff.COEF00 = double2coeff(coeff4/acc);
                Lanczos6Coeff.COEF01 = double2coeff(coeff5/acc);
                WriteVppRegisterValue(ui32Offset, Lanczos6Coeff.DW);
                ui32Offset+=4;
#endif
            }
        }
            
    }

    ui32Offset = VPP_VSCA_COEF00;
    if (ui32DstHeight>=ui32SrcHeight)
    {
        if (gsVppConfig.fVScalingRatioLast<1.0)
        {
            
            INT i;
            for (i = 27; i < 45; i++) 
            {
                WriteVppRegisterValue(ui32Offset, tap_filter_coeff[i]);
                ui32Offset += 4;
            }
        }
        gsVppConfig.fVScalingRatioLast=1.0;
    }
    else
    {
        if (((gsVppConfig.fVScalingRatioLast -(DOUBLE)ui32DstHeight/ui32SrcHeight)>10e-9) || 
            ((gsVppConfig.fVScalingRatioLast -(DOUBLE)ui32DstHeight/ui32SrcHeight)<-10e-9))
        {
            
            INT i;
            DOUBLE acc;

            gsVppConfig.fVScalingRatioLast = (DOUBLE)ui32DstHeight / ui32SrcHeight;    

            for (i = 0; i < 9; i++) 
            {
#if 1
                DOUBLE frac = (DOUBLE)i / (DOUBLE)(1 << 4); 

                DOUBLE coeff0 = lanczos4((1.0 + frac) * gsVppConfig.fVScalingRatioLast);
                DOUBLE coeff1 = lanczos4((0.0 + frac) * gsVppConfig.fVScalingRatioLast);
                DOUBLE coeff2 = lanczos4((1.0 - frac) * gsVppConfig.fVScalingRatioLast);
                DOUBLE coeff3 = lanczos4((2.0 - frac) * gsVppConfig.fVScalingRatioLast);

                acc = coeff0 + coeff1 + coeff2 + coeff3;

                Lanczos4Coeff.COEF00 = double2coeff(coeff0/acc);
                Lanczos4Coeff.COEF01 = double2coeff(coeff1/acc);
                WriteVppRegisterValue(ui32Offset, Lanczos4Coeff.DW);
                ui32Offset+=4;
                Lanczos4Coeff.COEF00 = double2coeff(coeff2/acc);
                Lanczos4Coeff.COEF01 = double2coeff(coeff3/acc);
                WriteVppRegisterValue(ui32Offset, Lanczos4Coeff.DW);
                ui32Offset+=4;
#endif
            }
        }       
    }
}
#endif

VOID VppSoc_UpdateCoeff2(UINT32 *pFilterCoef)
{
    INT    i;
    UINT32 ui32Offset;
    REG_VPP_HSCA_COEF reg_VPP_HSCA_COEF;
    REG_VPP_VSCA_COEF reg_VPP_VSCA_COEF;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    ui32Offset = VPP_HSCA_COEF00;

    for (i = 0; i < 54; i += 2)
    {
        reg_VPP_HSCA_COEF.DW     = 0;
        reg_VPP_HSCA_COEF.COEF00 = pFilterCoef[i];
        reg_VPP_HSCA_COEF.COEF01 = pFilterCoef[i + 1];
        WriteVppRegisterValue(ui32Offset, reg_VPP_HSCA_COEF.DW);
        ui32Offset += 4;
    }

    ui32Offset = VPP_VSCA_COEF00;

    for (i = 54; i < 90; i += 2)
    {
        reg_VPP_VSCA_COEF.DW = 0;
        reg_VPP_VSCA_COEF.COEF00 = pFilterCoef[i];
        reg_VPP_VSCA_COEF.COEF01 = pFilterCoef[i + 1];
        WriteVppRegisterValue(ui32Offset, reg_VPP_VSCA_COEF.DW);
        ui32Offset += 4;
    }
}

VOID VppSoc_UpdateYUV2RGB(VPP_YUV2RGB_DATA *pRCoef, VPP_YUV2RGB_DATA *pGCoef, VPP_YUV2RGB_DATA *pBCoef)
{
    REG_VPP_RCOEF reg_VPP_RCOEF;
    REG_VPP_GCOEF reg_VPP_GCOEF;
    REG_VPP_BCOEF reg_VPP_BCOEF;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    reg_VPP_RCOEF.DW = 0;
    reg_VPP_RCOEF.C1 = pRCoef->ui32YCoeff;
    reg_VPP_RCOEF.C2 = pRCoef->ui32UCoeff;
    reg_VPP_RCOEF.C3 = pRCoef->ui32VCoeff;
    WriteVppRegisterValue(VPP_RCOEF, reg_VPP_RCOEF.DW);
    
    reg_VPP_GCOEF.DW = 0;
    reg_VPP_GCOEF.C1 = pGCoef->ui32YCoeff;
    reg_VPP_GCOEF.C2 = pGCoef->ui32UCoeff;
    reg_VPP_GCOEF.C3 = pGCoef->ui32VCoeff;
    WriteVppRegisterValue(VPP_GCOEF, reg_VPP_GCOEF.DW);
    
    reg_VPP_BCOEF.DW = 0;
    reg_VPP_BCOEF.C1 = pBCoef->ui32YCoeff;
    reg_VPP_BCOEF.C2 = pBCoef->ui32UCoeff;
    reg_VPP_BCOEF.C3 = pBCoef->ui32VCoeff;
    WriteVppRegisterValue(VPP_BCOEF, reg_VPP_BCOEF.DW);

    WriteVppRegisterValue(VPP_OFFSET1, pRCoef->ui32Offset);
    WriteVppRegisterValue(VPP_OFFSET2, pGCoef->ui32Offset);
    WriteVppRegisterValue(VPP_OFFSET3, pBCoef->ui32Offset);
}

VOID VppSoc_UpdateFormatEndian(BOOL bInputBigEndian,BOOL bOutputBigEndian)
{
    REG_VPP_CTRL reg_VPP_CTRL;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    reg_VPP_CTRL.DW = ReadVppRegisterValue(VPP_CTRL);
    reg_VPP_CTRL.ENDIAN_MODE = bInputBigEndian?1:0;
    reg_VPP_CTRL.OUT_ENDIAN_MODE = bOutputBigEndian?1:0;
    WriteVppRegisterValue(VPP_CTRL, reg_VPP_CTRL.DW);
}

VOID VppSoc_SetUserMode(BOOL bUser)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsVppConfig.bUserMode = bUser;
}

VOID VppSoc_Reset(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    __VppSoc_Reset();
}

VOID VPP_GetFuncTable(VPP_FUNCTIONTABLE *pTable)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    memset(pTable, 0, sizeof(VPP_FUNCTIONTABLE));

    pTable->pfnInitialize = VppSoc_Initialize;
    pTable->pfnTerminate = VppSoc_Terminate;
    pTable->pfnLock = VppSoc_Lock;
    pTable->pfnUnlock = VppSoc_Unlock;
    pTable->pfnAllocOverlay = VppSoc_AllocOverlay;

    /* Set parameters which will be set only once */
    pTable->pfnSetParames = VppSoc_SetParames;
    pTable->pfnSetBase = VppSoc_SetBase;
    pTable->pfnSetSize = VppSoc_SetSize;
    pTable->pfnStart = VppSoc_Start;
    pTable->pfnStop = VppSoc_Stop;
    pTable->pfnIsBusy = VppSoc_IsBusy;
    pTable->pfnClearDMAInterrupt = VppSoc_ClearDMAInterrupt;
    pTable->pfnEnableDMAInterrupt = VppSoc_EnableDMAInterrupt;
    pTable->pfnDisableDMAInterrupt = VppSoc_DisableDMAInterrupt;
    pTable->pfnIsDMAInterrupted = VppSoc_IsDMAInterrupted;
    pTable->pfnUpdateFormatEndian = VppSoc_UpdateFormatEndian;

    pTable->pfnSleep = VppSoc_Sleep;
    pTable->pfnWakeup = VppSoc_Wakeup;
    pTable->pfnSetColorCtrl = VppSoc_SetColorCtrl;
    pTable->pfnSetInterlace = VppSoc_SetInterlace;
#if defined(_WIN32_WCE)
    pTable->pfnUpdateCoeff = VppSoc_UpdateCoeff;
    pTable->pfnUpdateHue = VppSoc_UpdateHue;
    pTable->pfnUpdateSaturation = VppSoc_UpdateSaturation;
#endif
    pTable->pfnUpdateBright = VppSoc_UpdateBright;
    pTable->pfnUpdateContrast = VppSoc_UpdateContrast;
    pTable->pfnUpdateCoeff2  = VppSoc_UpdateCoeff2;
    pTable->pfnUpdateYUV2RGB = VppSoc_UpdateYUV2RGB;
    pTable->pfnPrintRegister = VppSoc_PrintRegister;

    /* Internal function, debug only */
    pTable->pfnGetSourceInfo = VppSoc_GetSourceInfo;
    pTable->pfnGetSourceBuffer = VppSoc_GetSourceBuffer;
    pTable->pfnGetDestInfo = VppSoc_GetDestInfo;
    pTable->pfnGetDestBuffer = VppSoc_GetDestBuffer;
    pTable->pfnSetUserMode = VppSoc_SetUserMode;
    pTable->pfnReset = VppSoc_Reset;
}


