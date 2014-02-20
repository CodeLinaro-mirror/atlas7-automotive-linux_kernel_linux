/*
 * CSR sirfsoc LCD internal library
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include "CspSocLcdInternal.h"

volatile UINT8 *gpui8LcdRegs = NULL;



#if defined(_WIN32_WCE)
VOID __LcdSoc_Reset(VOID)
{
    WRITE_BITFIELD(struct ResetSWRBits, &v_pRstRegs->resetswrReg, lcd, 1);
    usWait(10);
    WRITE_BITFIELD(struct ResetSWRBits, &v_pRstRegs->resetswrReg, lcd, 0);
    usWait(10);
}

VOID __LcdSoc_EnableClock(VOID)
{
    WRITE_BITFIELD(struct clkclkenable, &(v_pClkRegs->clk_clkenable), lcd, 1);
}

VOID OSGetPanelInfo(LCD_PANEL_INFO *psPanel)
{
	psPanel->pfnPrePowerUp = LcdBsp_PrePowerUp;
	psPanel->pfnPostPowerUp = LcdBsp_PostPowerUp;
	psPanel->pfnPrePowerDown = LcdBsp_PrePowerDown;
	psPanel->pfnPostPowerDown = LcdBsp_PostPowerDown;

	PanelParamSetup(psPanel);

	psPanel->eMaxLayer = DISPLAY_LAYER-1;
	if ((psPanel->eMaxLayer <= 1) && 
		(psPanel->eOutFormat == LCD_OUT_24BIT_RBG888))
	{
		/* Atalas family has only 16 bit bus */
		psPanel->eOutFormat = LCD_OUT_18BIT_RBG666;
	}

	psPanel->ui32SysClock = v_pDriverGlobals->sysclk.dwSystemClock;
	psPanel->ui32FreshRate = LCD_DEFAULT_REFRESH_RATE;
}

#else

VOID OSGetPanelInfo(LCD_PANEL_INFO *psPanel)
{
	LCD_ASSERT(0);
}

#endif

VOID __LcdSoc_GetScreenSize(UINT32 *pui32Width, 
								UINT32 *pui32Height, 
								LCD_PANEL_INFO *psPanel)
{
	switch(psPanel->eOutFormat)
	{
	case LCD_OUT_8_BIT_RBGRBG:
		*pui32Width = (psPanel->ui32HEnd - psPanel->ui32HStart)/3 + 1;
		break;
	case LCD_OUT_8_BIT_YUV422:
		*pui32Width = (psPanel->ui32HEnd - psPanel->ui32HStart)/2 + 1;
		break;
	default:
		*pui32Width = psPanel->ui32HEnd - psPanel->ui32HStart + 1;
		break;
	}
	*pui32Height = psPanel->ui32VEnd - psPanel->ui32VStart + 1;
}


VOID __LcdSoc_SetPanel(LCD_PANEL_INFO *psPanel)
{
	REG_S0_TIM_CTRL reg_S0_TIM_CTRL;
    REG_S0_OSC_RATIO reg_S0_OSC_RATIO;
    BOOL bTVMode = __LcdSoc_IsTVMode(psPanel);
	UINT32 ui32PixClk = PIXEL_CLOCK(psPanel->ui32FreshRate,
		psPanel->ui32HsyncPeriod,
		psPanel->ui32VsyncPeriod);
    REG_S0_DISP_MODE reg_S0_DISP_MODE;
	REG_S0_VSYNC_WIDTH reg_S0_VSYNC_WIDTH;

	reg_S0_OSC_RATIO.DW = 0;
	reg_S0_OSC_RATIO.HALF_DUTY = 1;

	/* Different from Prima. PrimaII needn't stop pixel clock when underflow. 
	** HW will auto recover for next vsync. So keep reg_S0_OSC_RATIO.PCLK_CTRL=0  
	*/
	if (bTVMode)
	{
		reg_S0_OSC_RATIO.DIV_RATIO = 0x8; /*TODO: TV need 27M clock*/
	}
	else
	{
		INT iDivRatio = (psPanel->ui32SysClock/ui32PixClk) - 1;
		/* iDivRatio may <= 0 on FPGA, 
		** threshold 2 works on PrimaII FPGA, may need to adjust it for EVT verification stage
		*/
		if (iDivRatio < 2)
		{
			reg_S0_OSC_RATIO.DIV_RATIO = 2;
		}
		else
		{
			reg_S0_OSC_RATIO.DIV_RATIO = iDivRatio; 	   
		}

	}
	WriteLcdRegisterValue(S0_OSC_RATIO, reg_S0_OSC_RATIO.DW);

	reg_S0_TIM_CTRL.DW = 0;
	if (psPanel->bIOMaster)
	{
		reg_S0_TIM_CTRL.PCLK_IO = 1;
		reg_S0_TIM_CTRL.HSYNC_IO = 1;
		reg_S0_TIM_CTRL.VSYNC_IO = 1;
	}
	else
	{
		reg_S0_TIM_CTRL.PCLK_IO = 0;
		reg_S0_TIM_CTRL.HSYNC_IO = 0;
		reg_S0_TIM_CTRL.VSYNC_IO = 0;
	}
	if (psPanel->bPClkPolar)
	{
		reg_S0_TIM_CTRL.PCLK_POLAR = 1;
	}
	if (psPanel->bPClkEdge)
	{
		reg_S0_TIM_CTRL.PCLK_EDGE = 1;
	}
	if (psPanel->bHSyncPolar)
	{
		reg_S0_TIM_CTRL.HSYNC_POLAR = 1;
	}
	if (psPanel->bVSyncPolar)
	{
		reg_S0_TIM_CTRL.VSYNC_POLAR = 1;
	}
	reg_S0_TIM_CTRL.SYNC_DLY = psPanel->ui32HSyncDelay;
	
	WriteLcdRegisterValue(S0_TIM_CTRL, reg_S0_TIM_CTRL.DW);
	WriteLcdRegisterValue(S0_RGB_SEQ, psPanel->ui32RGBSequence);

	
	WriteLcdRegisterValue(S0_HSYNC_PERIOD, psPanel->ui32HsyncPeriod);
	WriteLcdRegisterValue(S0_HSYNC_WIDTH, psPanel->ui32HsyncWidth);
	WriteLcdRegisterValue(S0_VSYNC_PERIOD, psPanel->ui32VsyncPeriod);
	
	reg_S0_VSYNC_WIDTH.VSYNC_WIDTH = psPanel->ui32VsyncWidth;
	reg_S0_VSYNC_WIDTH.WITDTH_UINT = 1;
	WriteLcdRegisterValue(S0_VSYNC_WIDTH, reg_S0_VSYNC_WIDTH.DW);
	WriteLcdRegisterValue(S0_ACT_HSTART, psPanel->ui32HStart);
	WriteLcdRegisterValue(S0_ACT_VSTART, psPanel->ui32VStart);
	WriteLcdRegisterValue(S0_ACT_HEND, psPanel->ui32HEnd);
	WriteLcdRegisterValue(S0_ACT_VEND, psPanel->ui32VEnd);

	reg_S0_DISP_MODE.DW = 0;
	reg_S0_DISP_MODE.TOP_LAYER = psPanel->eLayer;
	reg_S0_DISP_MODE.OUT_FORMAT = psPanel->eOutFormat;
	reg_S0_DISP_MODE.FRAME_VALID = 1;
	WriteLcdRegisterValue(S0_DISP_MODE, reg_S0_DISP_MODE.DW);

	/* backlight scaling setting */
	WriteLcdRegisterValue(LCD_BLS_CTRL1,
			((psPanel->ui32HEnd - psPanel->ui32HStart + 1) << 20) |
			((psPanel->ui32VEnd - psPanel->ui32VStart + 1) << 9) |
			64);

	WriteLcdRegisterValue(LCD_BLS_CTRL2, (15 << 4) | (0));
	WriteLcdRegisterValue(LCD_BLS_LEVEL_TB0, (0) | (2 << 8) | (4 << 16) | (6 << 24));
	WriteLcdRegisterValue(LCD_BLS_LEVEL_TB1, (8) | (10 << 8) | (12 << 16) | (14 << 24));
	WriteLcdRegisterValue(LCD_BLS_LEVEL_TB2, (16) | (18 << 8) | (20 << 16) | (22 << 24));
	WriteLcdRegisterValue(LCD_BLS_LEVEL_TB3, (24) | (26 << 8) | (28 << 16) | (30 << 24));
}

VOID __LcdSoc_ConfigScreen(LCD_PANEL_INFO *psPanel)
{
    REG_SCR_CTRL reg_SCR_CTRL;
    REG_S0_INT_LINE reg_S0_INT_LINE;
    REG_S0_YUV_CTRL reg_S0_YUV_CTRL;
    REG_S0_TV_FIELD reg_S0_TV_FIELD;
    REG_S0_BLANK reg_S0_BLANK;

    BOOL bTVMode = __LcdSoc_IsTVMode(psPanel);

    /* Config screen */
    WriteLcdRegisterValue(INT_MASK, 0);
    WriteLcdRegisterValue(INT_CTRL_STATUS, 0xffff);

	/* Panel related */
	__LcdSoc_SetPanel(psPanel);

    /* Debug purpose: to check if we set right SCN_*_VAL */
    reg_S0_BLANK.DW = 0xff0000;
	reg_S0_BLANK.BLANK_VALID = 1;
    WriteLcdRegisterValue(S0_BLANK, reg_S0_BLANK.DW);
    WriteLcdRegisterValue(S0_BACK_COLOR, 0);

    reg_SCR_CTRL.DW = 0;
    reg_SCR_CTRL.SCREEN0_EN = 1;
    WriteLcdRegisterValue(SCR_CTRL, reg_SCR_CTRL.DW);
    reg_S0_INT_LINE.DW = 0;
    reg_S0_INT_LINE.INT_LINE_VALID = 1;
    WriteLcdRegisterValue(S0_INT_LINE, reg_S0_INT_LINE.DW);

    WriteLcdRegisterValue(S0_RGB_YUV_COEF1, 0x00428119);
    WriteLcdRegisterValue(S0_RGB_YUV_COEF2, 0x00264A70);
    WriteLcdRegisterValue(S0_RGB_YUV_COEF3, 0x00705E12);
    WriteLcdRegisterValue(S0_RGB_YUV_OFFSET, 0x00108080);
    
    reg_S0_YUV_CTRL.DW = 0;
    reg_S0_YUV_CTRL.YUV_SEQ = 1; /*YVYU sequence*/
    reg_S0_YUV_CTRL.Even_UV = 1;
    reg_S0_TV_FIELD.DW = 0;
    reg_S0_TV_FIELD.TV_HSTART = 0x339;
    reg_S0_TV_FIELD.TV_VSTART = 0x106;
    if(bTVMode)
    {
        reg_S0_YUV_CTRL.RGB_YUV = 1;
        reg_S0_TV_FIELD.TV_F_VALID = 1;
    }
    WriteLcdRegisterValue(S0_YUV_CTRL, reg_S0_YUV_CTRL.DW);
    WriteLcdRegisterValue(S0_TV_FIELD, reg_S0_TV_FIELD.DW);


}

VOID __LcdSoc_ConfigLayer0(UINT32 ui32PrimBase, 
					LCD_PIXELFORMAT ui32Format,
					LCD_PANEL_INFO *psPanel)
{
    REG_S0_LAYER_SEL reg_S0_LAYER_SEL;
    REG_L0_ALPHA reg_L0_ALPHA;
    REG_L0_CTRL reg_L0_CTRL;
    REG_L0_DMA_CTRL reg_L0_DMA_CTRL;
	UINT w,h;
    UINT32 ui32BitPerPixel;

    BOOL bTVMode = __LcdSoc_IsTVMode(psPanel);
    LCD_LAYER eLayer = psPanel->eLayer;

	__LcdSoc_GetScreenSize(&w, &h, psPanel);

    /* Config Layer 0 */
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HSTART), psPanel->ui32HStart);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HEND), psPanel->ui32HEnd);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_VSTART), psPanel->ui32VStart);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_VEND), psPanel->ui32VEnd);

    reg_L0_ALPHA.DW = 0;
    reg_L0_ALPHA.ALPHA_VAL = 0xff;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_ALPHA), reg_L0_ALPHA.DW);

    if (ui32Format == LCD_PIXELFORMAT_8888
	|| ui32Format == LCD_PIXELFORMAT_BGRX_8880)
    {
	ui32BitPerPixel = 32;
    }
    else
    {
	ui32BitPerPixel = 16;
    }
{
    DWORD dwDMAUnit = __LcdSoc_DMA_UNIT(bTVMode, FALSE);
    DWORD dwDisplayStride = BYTE_STRIDE(w, ui32BitPerPixel);
	DWORD dwXSize, dwYSize, dwSkip, dwSuppress;
	REG_L0_FIFO_CHK reg_Lx_FIFO_CHK;

	dwXSize 	= ((dwDisplayStride + dwDMAUnit- 1) / dwDMAUnit) - 1;
	dwSuppress	= ((dwXSize + 1) * dwDMAUnit - dwDisplayStride)>>3;

	reg_L0_DMA_CTRL.DW = 0;
	reg_L0_DMA_CTRL.SUPPRESS_QW_NUM = dwSuppress;
	reg_L0_DMA_CTRL.DMA_UNIT = (dwDMAUnit >> 3) - 1;
	reg_L0_DMA_CTRL.DMA_MODE = 1;

	if (bTVMode)
	{
		dwYSize		= h/2 - 1;
		dwSkip		= dwDisplayStride + dwDisplayStride - (dwXSize * dwDMAUnit);


        reg_L0_DMA_CTRL.DMA_CHAIN_MODE = 1;

		WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE1), ui32PrimBase + dwDisplayStride);
	}
	else
	{
		dwYSize 	= h - 1;
		dwSkip		= dwDisplayStride - (dwXSize * dwDMAUnit);
	}


    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_XSIZE), dwXSize);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_SKIP), dwSkip);

    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_YSIZE), dwYSize);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL), reg_L0_DMA_CTRL.DW);

    reg_Lx_FIFO_CHK.DW = 0;
    reg_Lx_FIFO_CHK.L0_LO_CHK = 0xF0;
    reg_Lx_FIFO_CHK.L0_MI_CHK = 0x80;
    reg_Lx_FIFO_CHK.L0_REQ_SEL = 1;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_FIFO_CHK), reg_Lx_FIFO_CHK.DW);

}

    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE0), ui32PrimBase);

    reg_L0_CTRL.DW = 0;
    reg_L0_CTRL.REPLICATE = LCD_DEFAULT_REPLICATE;
    reg_L0_CTRL.FIFO_RESET = 1;

    /* enable source alpha default for E_LO_CTRL_BPP_ARGB8888 */
    reg_L0_CTRL.BPP = __LcdSoc_EFormatToHwFormat(ui32Format);
    if (reg_L0_CTRL.BPP == E_LO_CTRL_BPP_ARGB8888)
    {
        reg_L0_CTRL.PREMULTI_ALPHA = 0;
        reg_L0_CTRL.SOURCE_ALPHA = 1;
    }
    else
    {
        reg_L0_CTRL.PREMULTI_ALPHA = LCD_DEFAULT_PREMULTI_ALPHA;
        reg_L0_CTRL.SOURCE_ALPHA = 0;
    }

    reg_L0_CTRL.CONFIRM = 1;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_L0_CTRL.DW);
    reg_L0_CTRL.FIFO_RESET = 0;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_L0_CTRL.DW);

    reg_S0_LAYER_SEL.DW = 0;
    reg_S0_LAYER_SEL.LAYER_SEL = (1 << eLayer);
    WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);
}
VOID __LcdSoc_PowerUp(UINT32 ui32PrimBase, 
					LCD_PIXELFORMAT ui32Format,
					LCD_PANEL_INFO *psPanel)
{
    __LcdSoc_EnableClock();
    __LcdSoc_Reset();

    __LcdSoc_ConfigScreen(psPanel);
    __LcdSoc_ConfigLayer0(ui32PrimBase, ui32Format, psPanel);
}

VOID LCD_BootUp(VOID* pLcdRegs, 
					UINT32 ui32PrimBase, 
					UINT32 ui32BitPerPixel,
					LCD_PANEL_INFO *psPanel)
{
	LCD_PANEL_INFO sPanelInfo;
	LCD_PIXELFORMAT ui32Format;
	LCD_ASSERT(pLcdRegs);
	gpui8LcdRegs = (volatile UINT8 *)pLcdRegs;
	if (!psPanel)
	{
		psPanel = &sPanelInfo;
		OSGetPanelInfo(psPanel);
	}

	ui32Format = (ui32BitPerPixel == 16) ? LCD_PIXELFORMAT_565 : LCD_PIXELFORMAT_8888;

	psPanel->pfnPrePowerUp();	
	__LcdSoc_PowerUp(ui32PrimBase, ui32Format, psPanel);
	psPanel->pfnPostPowerUp();
}




