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
@Date           20 December 2009

@Platform       Generic

@Description    Csp Prima2 APIs
****************************************************************************/

#include "CspSocLcdInternal.h"

/***************************************************************************
** 
** OS Dependent
****************************************************************************/
#if defined(_WIN32_WCE)
	
	VOID OSWaitms(UINT uCount)
	{
		msWait(uCount);
	}
	
	VOID OSGetFBBase(LCD_GETVIDMEM_DATA *pData)
	{
		pData->ui32PBase = LCD_FRAME_BUF_PHYS_ADDR;
		pData->ui32VBase = LCD_FRAME_BUF_VIRT_ADDR;
	}
	
	VOID OSGetFBSize(UINT32 *pui32FBSize)
	{
		UINT32	ui32Ret, ui32Type = 0, ui32Size, ui32FBSize, ui32SGXSize;
		HKEY	hConfig = (HKEY)INVALID_HANDLE_VALUE;
		BOOL bFBMemSet=FALSE; /*, bSGXMemSet=FALSE, bSharedSet=FALSE;*/
		UINT uiPrimarySize;
	
		__LcdSoc_GetPrimarySize(&uiPrimarySize);
		
		ui32FBSize = MEM_DISPLAY_SIZE;
		ui32SGXSize = 0;
	
		/*
			read the registry to get the mode config
			Open key in the registry.
		*/
		ui32Ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
								   TEXT("Drivers\\Display\\LCD"),
								   0, 0, &hConfig);
		/* Load the Configuration data for the driver */
		if (ERROR_SUCCESS == ui32Ret)
		{
			/* Read resolution overrides from registry, if they exists */
			ui32Size = sizeof(ULONG);
			
	
			ui32Ret = RegQueryValueEx (hConfig, TEXT("sizeDCMem"), 0, (LPDWORD)&ui32Type, (PUCHAR)&ui32FBSize, (LPDWORD)&ui32Size);
			if ((ui32Ret == ERROR_SUCCESS) && (ui32Type == REG_DWORD))		/* Binary format */
			{
				bFBMemSet = TRUE;
				if (ui32FBSize <= uiPrimarySize)
				{
					ui32FBSize = uiPrimarySize;
				}
				if (ui32FBSize > MEM_DISPLAY_SIZE)
				{
					ui32FBSize = MEM_DISPLAY_SIZE;
				}
			}
			ui32Ret = RegQueryValueEx (hConfig, TEXT("sizeSGXMem"), 0, (LPDWORD)&ui32Type, (PUCHAR)&ui32SGXSize, (LPDWORD)&ui32Size);
			if ((ui32Ret == ERROR_SUCCESS) && (ui32Type == REG_DWORD))		/* Binary format */
			{
				/*bSGXMemSet = TRUE;*/
				if (!bFBMemSet && (MEM_DISPLAY_SIZE - ui32SGXSize)> uiPrimarySize)
				{
					ui32FBSize = MEM_DISPLAY_SIZE - ui32SGXSize;
				}
				if (ui32SGXSize > MEM_DISPLAY_SIZE)
				{
					ui32SGXSize = MEM_DISPLAY_SIZE;
				}
			}
			else
			{
				if (ui32FBSize==uiPrimarySize)
				{
					ui32SGXSize = MEM_DISPLAY_SIZE - uiPrimarySize;
				}
			}
	
			if(ui32FBSize+ui32SGXSize > MEM_DISPLAY_SIZE)
			{
				ui32SGXSize = MEM_DISPLAY_SIZE - ui32FBSize;
			}
			
			ui32Ret = RegSetValueEx(hConfig, TEXT("sizeDCMemSaved"), 0, REG_DWORD, (BYTE*)&ui32FBSize, 4);
			if (ui32Ret != ERROR_SUCCESS)
			{
				LCD_MSG(("%s: Fail to set registry sizeDCMemSaved", __FUNCTION__));
			}
			RegSetValueEx(hConfig, TEXT("sizeSGXMemSaved"), 0, REG_DWORD, (BYTE*)&ui32SGXSize, 4);
			if (ui32Ret != ERROR_SUCCESS)
			{
				LCD_MSG(("%s: Fail to set registry sizeSGXMemSaved", __FUNCTION__));
			}
			
			LCD_MSG(("DCMemSize: 0x%x\n", ui32FBSize));
			LCD_MSG(("SGXMemSize: 0x%x\n", ui32SGXSize));
			RegCloseKey(hConfig);
		}	 
		else
		{
			LCD_MSG(("%s: Fail to open registry", __FUNCTION__));
		}
		*pui32FBSize = ui32FBSize;
	}
	
	VOID __LcdSoc_DisableClock(VOID)
	{
		
		WRITE_BITFIELD(struct clkclkenable, &(v_pClkRegs->clk_clkenable), lcd, 0);
	}
	
	VOID *OSMapLcdRegs()
	{
		if (CspRegMap(FALSE))
		{
			return (VOID*)v_pLcdRegs;
		}
		else
		{
			LCD_MSG(("OSMapLcdRegs: CspRegMap fail\n"));
			return NULL;
		}
	}
	
	VOID OSUnmapLcdRegs()
	{
		CspRegUnMap();
	}
	
	VOID *OSGetVppRegs(VOID)
	{
		return (VOID*)v_pVppRegs;
	}
	
	BOOL __LcdSoc_GetVPP(VOID)
	{
		UINT32	ui32Ret, ui32Type = 0, ui32Size = sizeof(UINT32);
		HKEY	hConfig = (HKEY)INVALID_HANDLE_VALUE;
		UINT32 ui32HWVpp;
		BOOL bRet = FALSE;
		/*
			read the registry to get the mode config
			Open key in the registry.
		*/
		ui32Ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
								   TEXT("Drivers\\Display\\LCD"),
								   0, 0, &hConfig);
	
		if (ERROR_SUCCESS == ui32Ret)
		{	 
			ui32Ret = RegQueryValueEx (hConfig, TEXT("HWVPP"), 0, &ui32Type, (PUCHAR)&ui32HWVpp, &ui32Size);
			if ((ui32Ret == ERROR_SUCCESS) && (ui32Type == REG_DWORD))		/* Binary format */
			{
				bRet = ui32HWVpp ? TRUE : FALSE;
			}
			else
			{
				LCD_MSG(("%s: Fail to open registry Drivers\\Display\\LCD\\HWVPP", __FUNCTION__));
			}
			RegCloseKey(hConfig);
		}
		else
		{
			LCD_MSG(("%s: Fail to open registry Drivers\\Display\\LCD", __FUNCTION__));
		}
	
		return bRet;
	}
	
	BOOL OSLoadVpp(VOID* *phVppHandle, VPP_FUNCTIONTABLE *pVppFuncTable)
	{
		if(__LcdSoc_GetVPP())
		{
			*phVppHandle = LoadLibrary(VPP_MODULE_NAME);
		}
		else
		{
			*phVppHandle = NULL;
			return FALSE;
		}
		if (*phVppHandle)
		{
			PFNVPP_GETFUNCTABLE pfnVPP_GetFuncTable;
			pfnVPP_GetFuncTable = (PFNVPP_GETFUNCTABLE)GetProcAddress(gsLcdConfig.hVppHandle, _T("VPP_GetFuncTable"));
			if (pfnVPP_GetFuncTable)
			{
				pfnVPP_GetFuncTable(pVppFuncTable);
			}
			else
			{
				return FALSE;
			}
		}
		return TRUE;
	}
	
	VOID OSUnloadVpp(VOID* hVppHandle)
	{
		FreeLibrary(hVppHandle);
	}
	
	
	
#else

	VOID OSWaitms(UINT uCount)
	{
		msleep(uCount);
	}
	
	VOID OSGetFBBase(LCD_GETVIDMEM_DATA *pData)
	{
	}
	
	VOID OSGetFBSize(UINT32 *pui32FBSize)
	{
	}
	
	VOID *OSMapLcdRegs(VOID)
	{
		LCD_ASSERT(0);
		return NULL;
	}
	VOID OSUnmapLcdRegs(VOID)
	{
	}
	
	VOID *OSGetVppRegs(VOID)
	{
		return NULL;
	}
	BOOL OSLoadVpp(VOID* *phVppHandle, VPP_FUNCTIONTABLE *pVppFuncTable)
	{
		*phVppHandle = (VOID *)0xabcdabcd;
		VPP_GetFuncTable(pVppFuncTable);
		return TRUE;
	}
	
	VOID OSUnloadVpp(VOID* hVppHandle)
	{
	}
	
#endif

/***************************************************************************
** 
** OS Independent
****************************************************************************/

/* 
** Exported function declaration 
*/

VOID LcdSoc_PrintRegister(VOID);
VOID LcdSoc_Reset(VOID);

BOOL LcdSoc_Initialize(VOID *pLcdRegs, 
						VOID *pVppRegs,
						UINT32 ui32PrimBase, 
						UINT32 ui32BitPerPixel,
						LCD_PANEL_INFO *psPanel);
VOID LcdSoc_Terminate(VOID);
BOOL LcdSoc_Wakeup(VOID);
VOID LcdSoc_Sleep(VOID);
VOID LcdSoc_GetScanLine(LCD_GETSCANLINE_DATA *pData);
VOID LcdSoc_WaitForVBlank(LCD_WAITFORVBLANK_DATA *pVBlankData);
VOID LcdSoc_GetMode(LCD_GETMODE_DATA *pDisplayMode);

VOID LcdSoc_ClearVsyncInterrupt(VOID);
VOID LcdSoc_EnableVsyncInterrupt(VOID);
VOID LcdSoc_DisableVsyncInterrupt(VOID);
BOOL LcdSoc_IsVsyncInterrupted(VOID);
VOID LcdSoc_ClearDMAInterrupt(LCD_LAYER eLayer);
VOID LcdSoc_EnableDMAInterrupt(LCD_LAYER eLayer);
VOID LcdSoc_DisableDMAInterrupt(LCD_LAYER eLayer);
BOOL LcdSoc_IsDMAInterrupted(LCD_LAYER eLayer);

VOID LcdSoc_EnableAlphaBlend(LCD_LAYER eLayer, BOOL flag);

LCD_LAYER LcdSoc_AllocOverlay(LCD_ALLOCOVERLAY_DATA *pData);
VOID LcdSoc_FreeOverlay(LCD_LAYER eLayer);

VOID LcdSoc_ShowOverlay(LCD_LAYER eLayer);
BOOL LcdSoc_SetParameters(LCD_SETPARAMS_DATA *pData);
VOID LcdSoc_GetParameters(LCD_SETPARAMS_DATA *pData);

VOID LcdSoc_HideOverlay(LCD_LAYER eLayer);
VOID LcdSoc_SetOverlayPos(LCD_LAYER eLayer, RECT *pSrc, RECT *pDst);
VOID LcdSoc_FlipOverlay(LCD_LAYER eLayer, UINT32 ui32Base, LCD_FLIP_MODE eField);

VOID LcdSoc_SetCursorShape(UINT32 *pMask, INT iMaskStride, 
    UINT32 *pColor, INT iXHot, INT iYHot, INT iWidth, INT iHeight);
VOID LcdSoc_MoveCursor(INT iXPos, INT iYPos);
VOID LcdSoc_SetCursorRotate(INT iAngle);
VOID LcdSoc_SetPixelClock(UINT32 ui32PixelClock);
UINT32 LcdSoc_GetPixelClock(VOID);
VOID LcdSoc_ClearInterrupt(LCD_INTERRUPT_TYPE eType);
VOID LcdSoc_EnableInterrupt(LCD_INTERRUPT_TYPE eType);
VOID LcdSoc_DisableInterrupt(LCD_INTERRUPT_TYPE eType);
UINT32 LcdSoc_IsInterrupted(LCD_INTERRUPT_TYPE eType);


/*
** Global variables
*/

static VPP_FUNCTIONTABLE gsVPPFuncTable;
LCDSOC_CONFIG gsLcdConfig;
LCD_PANEL_INFO gsPanelInfo;

UINT32 g_eHwFormatToBpp[] =
{
/*Question: BPP of E_LO_CTRL_BPP_RGB666 should be 4?*/
    4, /*E_LO_CTRL_BPP_RGB666*/
    2, /*E_LO_CTRL_BPP_RGB565*/
    2, /*E_LO_CTRL_BPP_RGB556*/
    2, /*E_LO_CTRL_BPP_RGB655*/
    4, /*E_LO_CTRL_BPP_RGB888*/
    4, /*E_LO_CTRL_BPP_TRGB888*/
    4, /*E_LO_CTRL_BPP_ARGB8888*/
    2, /*E_LO_CTRL_BPP_UNKNOWN*/
};

ENUM_LO_CTRL_BPP __LcdSoc_EFormatToHwFormat(LCD_PIXELFORMAT eFormat)
{
    switch(eFormat)
    {
    case LCD_PIXELFORMAT_565:
        return E_LO_CTRL_BPP_RGB565;
    case LCD_PIXELFORMAT_556:
        return E_LO_CTRL_BPP_RGB556;
    case LCD_PIXELFORMAT_655:
        return E_LO_CTRL_BPP_RGB655;
    case LCD_PIXELFORMAT_666:
        return E_LO_CTRL_BPP_RGB666;
    case LCD_PIXELFORMAT_BGRX_8880:
        return E_LO_CTRL_BPP_RGB888;
    case LCD_PIXELFORMAT_8888:
        return E_LO_CTRL_BPP_ARGB8888;
    default:
        LCD_ASSERT(0);
        break;
    }
    return E_LO_CTRL_BPP_UNKNOWN;
}

LCD_PIXELFORMAT __LcdSoc_HwFormatToEFormat(ENUM_LO_CTRL_BPP hwFormat)
{
    switch(hwFormat)
    {
    
    case E_LO_CTRL_BPP_RGB565:
        return LCD_PIXELFORMAT_565;
    case E_LO_CTRL_BPP_RGB556:
        return LCD_PIXELFORMAT_556;
    case E_LO_CTRL_BPP_RGB655:
        return LCD_PIXELFORMAT_655;
    case E_LO_CTRL_BPP_RGB666:
        return LCD_PIXELFORMAT_666;
    case E_LO_CTRL_BPP_RGB888:
        return LCD_PIXELFORMAT_BGRX_8880;
    case E_LO_CTRL_BPP_ARGB8888:
        return LCD_PIXELFORMAT_8888;
    default:
        LCD_ASSERT(0);
        break;
    }
    return LCD_PIXELFORMAT_UNKNOWN;
}

/***************************************************************************
 @Function  __LcdSoc_EFormatToBpp
 @Description
 Get bpp of PVRFormat 

 @input eFormat: interface format
 @Return   byte per pixel
****************************************************************************/
UINT32 __LcdSoc_EFormatToBpp(LCD_PIXELFORMAT eFormat)
{
    switch(eFormat)
    {
    case LCD_PIXELFORMAT_565:
    case LCD_PIXELFORMAT_556:
    case LCD_PIXELFORMAT_655:
        return 2;
    case LCD_PIXELFORMAT_666:
    case LCD_PIXELFORMAT_BGRX_8880:
    case LCD_PIXELFORMAT_8888:
        return 4;
    default:
        LCD_ASSERT(0);
        break;
    }
    return 2;
}

VOID __LcdSoc_WaitIdle(LCD_LAYER eLayer, BOOL bWithVpp)
{
    int     nTimeout;

    if (bWithVpp)
    {
        nTimeout = 0;
        while(gsVPPFuncTable.pfnIsBusy())
        {
            OSWaitms(1);
            nTimeout ++;
            if (nTimeout > 1000)
            {
                LCD_MSG(("wait vpp idle timeout\r\n"));
            }
        }
    }    
    nTimeout = 0;
    while(ReadLcdRegisterValue(DMA_STATUS) & (1<<eLayer))
    {
        OSWaitms(1);
        nTimeout ++;
        if (nTimeout > 1000)
        {
            LCD_MSG(("wait DMA_STATUS timeout\r\n"));
        }
    }
    nTimeout = 0;
    while(ReadLcdRegisterValue(S0_LAYER_STATUS) & (1<<eLayer))
    {
        OSWaitms(1);
        nTimeout ++;
        if (nTimeout > 1000)
        {
            LCD_MSG(("wait S0_LAYER_STATUS timeout\r\n"));
        }
    }
}

VOID __LcdSoc_DisableLayer(LCD_LAYER eLayer, BOOL bWait)
{
    REG_S0_LAYER_SEL reg_S0_LAYER_SEL;
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);

    reg_S0_LAYER_SEL.DW = ReadLcdRegisterValue(S0_LAYER_SEL);
    if (reg_S0_LAYER_SEL.LAYER_SEL & (1<<eLayer))
    {
        reg_S0_LAYER_SEL.LAYER_SEL &= ~(1<<eLayer);
        WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);

        if (psLayerState->bNeedVpp)
        {
            REG_L0_DMA_CTRL reg_L0_DMA_CTRL;
            reg_L0_DMA_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL));
            reg_L0_DMA_CTRL.VPP_PASS_MODE = 0;
            WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL), reg_L0_DMA_CTRL.DW);
        
            __LcdSoc_ConfirmLayerSetting(eLayer);
        }

        if (bWait)
        {
            __LcdSoc_WaitIdle(eLayer, psLayerState->bNeedVpp);
        }
    }
}

VOID __LcdSoc_EnableLayer(LCD_LAYER eLayer)
{
    
    REG_S0_LAYER_SEL reg_S0_LAYER_SEL;
    REG_L0_DMA_CTRL reg_L0_DMA_CTRL;
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);

    reg_S0_LAYER_SEL.DW = ReadLcdRegisterValue(S0_LAYER_SEL);
    if (!(reg_S0_LAYER_SEL.LAYER_SEL & (1<<eLayer)))
    {
        __LcdSoc_WaitIdle(eLayer, psLayerState->bNeedVpp);
        __LcdSoc_ResetLayerFifo(eLayer);

        reg_L0_DMA_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL));
        if (psLayerState->bNeedVpp)
        {
            reg_L0_DMA_CTRL.VPP_PASS_MODE = 1;
        }
        else
        {
            reg_L0_DMA_CTRL.VPP_PASS_MODE = 0;
        }
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL), reg_L0_DMA_CTRL.DW);
        __LcdSoc_ConfirmLayerSetting(eLayer);

        reg_S0_LAYER_SEL.LAYER_SEL |= (1<<eLayer);
        WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);
    }
}

VOID __LcdSoc_SetDma(LCD_LAYER eLayer, RECT *pRectSrc)
{
    REG_L0_DMA_CTRL reg_Lx_DMA_CTRL;
    REG_L0_FIFO_CHK reg_Lx_FIFO_CHK;
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    UINT32 ui32Bpp = g_eHwFormatToBpp[__LcdSoc_EFormatToHwFormat(psLayerState->eLcdFormat)];

    /*Set DMA register configuration*/
    UINT32 ui32Width = pRectSrc->right - pRectSrc->left;
    UINT32 ui32Height = pRectSrc->bottom - pRectSrc->top;

    BOOL bTVMode = __LcdSoc_IsTVMode(&gsPanelInfo);
    UINT32 ui32DMAUnit = __LcdSoc_DMA_UNIT(bTVMode, eLayer!=LCD_PRIMARY);
    UINT32 uiOffset = (pRectSrc->top * psLayerState->ui32SurfWidth + pRectSrc->left) * ui32Bpp;
    UINT32 ui32XSize = (((uiOffset & 7) + ui32Width * ui32Bpp  + ui32DMAUnit -1 ) / ui32DMAUnit) - 1;

	UINT32 ui32YSize, ui32Skip;

	if (bTVMode)
	{
		ui32YSize = ui32Height/2 - 1;
		ui32Skip = (psLayerState->ui32SurfWidth* ui32Bpp)*2 - (ui32XSize * ui32DMAUnit);
	}
	else
	{
    	ui32YSize = ui32Height - 1; /*in line units*/
		ui32Skip = (psLayerState->ui32SurfWidth* ui32Bpp) - (ui32XSize * ui32DMAUnit);
	}

    /*Set overlay surface addr*/
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE0), psLayerState->ui32Base + uiOffset);
    if (bTVMode)
    {
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE1), psLayerState->ui32Base + uiOffset + psLayerState->ui32SurfWidth*ui32Bpp);    
    }

    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_XSIZE), ui32XSize);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_YSIZE), ui32YSize);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_SKIP),  ui32Skip);

    reg_Lx_FIFO_CHK.DW = 0;
    reg_Lx_FIFO_CHK.L0_LO_CHK = 0xF0;
    reg_Lx_FIFO_CHK.L0_MI_CHK = 0x80;
    reg_Lx_FIFO_CHK.L0_REQ_SEL = 1;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_FIFO_CHK), reg_Lx_FIFO_CHK.DW);

    reg_Lx_DMA_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL));
    reg_Lx_DMA_CTRL.SUPPRESS_QW_NUM = 
        ((ui32XSize + 1) * ui32DMAUnit - ui32Width * ui32Bpp - (uiOffset & 7)) >> 3;
    reg_Lx_DMA_CTRL.DMA_UNIT = (ui32DMAUnit >> 3) - 1;
    reg_Lx_DMA_CTRL.DMA_MODE = 1;
    if (bTVMode)
    {
        reg_Lx_DMA_CTRL.DMA_CHAIN_MODE = 1;
    }
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_DMA_CTRL), reg_Lx_DMA_CTRL.DW);    
}

UINT32 __LcdSoc_CKValue(LCD_PIXELFORMAT eFormat, BOOL bDuplicate, UINT32 value)
{
    if ((eFormat == LCD_PIXELFORMAT_BGRX_8880) ||
        (eFormat == LCD_PIXELFORMAT_8888))
    {
        return value;
    }
    else if (eFormat == LCD_PIXELFORMAT_565)
    {
        REG_L0_CKEYB_SRC reg_L0_CKEYB_SRC;
        reg_L0_CKEYB_SRC.DW = 0;
        reg_L0_CKEYB_SRC.R = ((value>>11) & 0x1f)<<3;
        reg_L0_CKEYB_SRC.G = ((value>>5) & 0x3f)<<2;
        reg_L0_CKEYB_SRC.B = (value & 0x1f)<<3;

        if (bDuplicate)
        {
            reg_L0_CKEYB_SRC.R |= (reg_L0_CKEYB_SRC.R >> 5);
            reg_L0_CKEYB_SRC.G |= (reg_L0_CKEYB_SRC.G >> 6);
            reg_L0_CKEYB_SRC.B |= (reg_L0_CKEYB_SRC.B >> 5);
        }
        return reg_L0_CKEYB_SRC.DW;
    }
    else if (eFormat >= LCD_PIXELFORMAT_UYVY )
    {
        return value;
    }
    else
    {
        LCD_ASSERT(0);
        return 0;
    }
}
BOOL __LcdSoc_SetParameters(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_PIXELFORMAT eFormat = psLayerState->eLcdFormat;
    REG_L0_CTRL reg_L0_CTRL;
	BOOL bRet = TRUE;

	if (!psLayerState->bShow)
	{
	    __LcdSoc_DisableLayer(eLayer, TRUE);
	}
    reg_L0_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL));
    if (psLayerState->bNeedVpp)
    {
        VPP_SETPARAMS_DATA sVPPParams;

        reg_L0_CTRL.BPP = VPP_TO_LCD_CTRL_BPP;
        memset(&sVPPParams, 0, sizeof(sVPPParams));
        sVPPParams.eSrcFormat = eFormat;
        sVPPParams.ui32SrcBase = psLayerState->ui32Base;
        sVPPParams.uiSrcWStride_pixel = psLayerState->ui32SurfWidth;
        sVPPParams.uiSrcHStride_pixel = psLayerState->ui32SurfHeight;
        sVPPParams.eDstFormat = VPP_TO_LCD_PIXELFORMAT;
        sVPPParams.ui32DstBase = 0;

        gsVPPFuncTable.pfnLock(TRUE);
        bRet = gsVPPFuncTable.pfnSetParames(&sVPPParams);
    }
    else
    {
        reg_L0_CTRL.BPP = __LcdSoc_EFormatToHwFormat(eFormat);
    }

    reg_L0_CTRL.REPLICATE = psLayerState->bReplicate?1:0;
    reg_L0_CTRL.PREMULTI_ALPHA = psLayerState->bPremultiAlpha?1:0;    
    reg_L0_CTRL.CONFIRM = 1;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_L0_CTRL.DW);

    /* Force to update src & dst rect related parameters */
    __LcdSoc_SetSize(eLayer, TRUE);
    __LcdSoc_Flip(eLayer, LCD_FLIP_FRAME);
    
    __LcdSoc_SetColorKey(eLayer);

    __LcdSoc_SetAlphaProperty(eLayer);
    __LcdSoc_SetGlobalAlpha(eLayer);
	__LcdSoc_ConfirmLayerSetting(eLayer);

    if (psLayerState->bShow)
    {
        __LcdSoc_EnableLayer(eLayer);
    }
	return bRet;
}

VOID __LcdSoc_Lock(LCD_LAYER eLayer, UINT32 ui32Base)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    UINT32 timeout = 10;
    while(ui32Base == psLayerState->ui32BaseOn)
    {
        OSWaitms(1);
        timeout--;
        LCD_ASSERT(timeout);
    }
}

VOID __LcdSoc_CalcSize(RECT *psRectSrcOrig, RECT *psRectDstOrig, BOOL bNeedVpp, RECT *psRectSrc, RECT *psRectDst)
{
    INT32 i32SrnWidth;
    INT32 i32SrnHeight;

    INT32 i32SrcOrigWidth, i32SrcOrigHeight;
    INT32 i32DstOrigWidth, i32DstOrigHeight;

    __LcdSoc_GetScreenSize((UINT32*)&i32SrnWidth, (UINT32*)&i32SrnHeight, &gsPanelInfo);

    i32SrcOrigWidth = psRectSrcOrig->right - psRectSrcOrig->left;
    i32DstOrigWidth = psRectDstOrig->right - psRectDstOrig->left;
    i32SrcOrigHeight = psRectSrcOrig->bottom - psRectSrcOrig->top;
    i32DstOrigHeight = psRectDstOrig->bottom - psRectDstOrig->top;

    if(psRectDstOrig->left < 0)
    {
        psRectDst->left = 0;
        psRectSrc->left = psRectSrcOrig->left+(i32SrcOrigWidth/i32DstOrigWidth*(-psRectDstOrig->left));
    }
    else if(psRectDstOrig->left > (i32SrnWidth-1))
    {
        psRectDst->left = i32SrnWidth-1;
        psRectDst->right = i32SrnWidth;

        psRectSrc->left = psRectSrcOrig->left;
        psRectSrc->right = psRectSrcOrig->left+1;;
    }
    else
    {
        psRectDst->left = psRectDstOrig->left;
		psRectSrc->left = psRectSrcOrig->left;
    }

    if(psRectDstOrig->right < 0)
    {
    	psRectDst->left = -1;
		psRectDst->right = 0;
		psRectSrc->left = psRectSrcOrig->right-1;
		psRectSrc->right = psRectSrcOrig->right;
    }
    else if(psRectDstOrig->right > i32SrnWidth)
    {
        psRectDst->right = i32SrnWidth;
        psRectSrc->right = psRectSrcOrig->right-(i32SrcOrigWidth/i32DstOrigWidth*(psRectDstOrig->right-i32SrnWidth));
    }
    else
    {
        psRectDst->right = psRectDstOrig->right;
        psRectSrc->right = psRectSrcOrig->right;
    }


    if(psRectDstOrig->top < 0)
    {
        psRectDst->top = 0;
        psRectSrc->top = psRectSrcOrig->top+(i32SrcOrigHeight/i32DstOrigHeight*(-psRectDstOrig->top));
    }
    else if(psRectDstOrig->top > (i32SrnHeight-1))
    {
        psRectDst->top = i32SrnHeight-1;
        psRectDst->bottom = i32SrnHeight;

        psRectSrc->top = psRectSrcOrig->top;
        psRectSrc->bottom = psRectSrcOrig->top+1;
    }
    else
    {
        psRectDst->top = psRectDstOrig->top;
        psRectSrc->top = psRectSrcOrig->top;
    }

    if(psRectDstOrig->bottom < 0)
    {
    	psRectDst->top = -1;
		psRectDst->bottom = 0;
		psRectSrc->top = psRectSrcOrig->bottom-1;
		psRectSrc->bottom = psRectSrcOrig->bottom;
    }
    else if(psRectDstOrig->bottom > i32SrnHeight)
    {
        psRectDst->bottom = i32SrnHeight;
        psRectSrc->bottom = psRectSrcOrig->bottom-(i32SrcOrigHeight/i32DstOrigHeight*(psRectDstOrig->bottom-i32SrnHeight));
    }
    else
    {
        psRectDst->bottom = psRectDstOrig->bottom;
        psRectSrc->bottom = psRectSrcOrig->bottom;
    }

    /* workaround LCD don't support 1 line display */
    if((psRectDst->bottom - psRectDst->top) == 1)
    {
        psRectDst->top = i32SrnHeight;
        psRectDst->bottom = i32SrnHeight+2;

        psRectSrc->top = 0;
        psRectSrc->bottom = 2;
    }

    /* workaround RGB overlay stretch, we don't support it, so
    *  show it as the smaller rect.
    */
    if (!bNeedVpp)
    {
        INT32   i32WidthSrc, i32HeightSrc;
        INT32   i32WidthDst, i32HeightDst;

        i32WidthSrc = psRectSrc->right - psRectSrc->left;
        i32HeightSrc = psRectSrc->bottom - psRectSrc->top;
        i32WidthDst = psRectDst->right - psRectDst->left;
        i32HeightDst = psRectDst->bottom - psRectDst->top;

        if (i32WidthSrc > i32WidthDst)
        {
            psRectSrc->right = psRectSrc->left + i32WidthDst;
        }
        else if (i32WidthSrc < i32WidthDst)
        {
            psRectDst->right = psRectDst->left + i32WidthSrc;
        }
        if (i32HeightSrc > i32HeightDst)
        {
            psRectSrc->bottom = psRectSrc->top + i32HeightDst;
        }
        else if (i32HeightSrc < i32HeightDst)
        {
            psRectDst->bottom = psRectDst->top + i32HeightSrc;
        }
    }
}


VOID __LcdSoc_SetDstRect(LCD_LAYER eLayer, RECT *psRectDstOn)
{
    switch(gsPanelInfo.eOutFormat)
    {
    case LCD_OUT_8_BIT_RBGRBG:
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HSTART), (psRectDstOn->left*3)+gsPanelInfo.ui32HStart);
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HEND), (psRectDstOn->right-1)*3+gsPanelInfo.ui32HStart);
        break;
    case LCD_OUT_8_BIT_YUV422:
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HSTART), (psRectDstOn->left*2)+gsPanelInfo.ui32HStart);
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HEND), (psRectDstOn->right-1)*2+gsPanelInfo.ui32HStart);
        break;
    default:
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HSTART), psRectDstOn->left+gsPanelInfo.ui32HStart);
        /*L0_HEND= L0_HSTART + width - 1*/
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_HEND), psRectDstOn->right+gsPanelInfo.ui32HStart-1);
        break;        
    }
    
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_VSTART), (psRectDstOn->top)+gsPanelInfo.ui32VStart);
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_VEND), psRectDstOn->bottom+gsPanelInfo.ui32VStart-1);    
}

VOID __LcdSoc_SetSize(LCD_LAYER eLayer, BOOL bForceUpdate)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    RECT sRectSrcOn, sRectDstOn;
    INT iRectSrcOnDiff=0, iRectDstOnDiff=0, iRectDstOnRangeDiff=0;

    __LcdSoc_CalcSize(&psLayerState->sRectSrc, &psLayerState->sRectDst, psLayerState->bNeedVpp, &sRectSrcOn, &sRectDstOn);
    if (!bForceUpdate)
    {
        iRectSrcOnDiff = memcmp(&sRectSrcOn, &psLayerState->sRectSrcOn, sizeof(RECT));
        iRectDstOnDiff = memcmp(&sRectDstOn, &psLayerState->sRectDstOn, sizeof(RECT));
        iRectDstOnRangeDiff = 
            ((sRectDstOn.right - sRectDstOn.left) != (psLayerState->sRectDstOn.right - psLayerState->sRectDstOn.left)) ||
            ((sRectDstOn.bottom - sRectDstOn.top) != (psLayerState->sRectDstOn.bottom - psLayerState->sRectDstOn.top));
    }
    if (psLayerState->bNeedVpp)
    {
        if (bForceUpdate || (iRectSrcOnDiff || iRectDstOnRangeDiff))
        {
            UINT32  ui32SrcSkip, ui32DstSkip;
            RECT    sVppRectDst;

            if ((psLayerState->eLcdFormat >= LCD_PIXELFORMAT_UYVY) && 
                (psLayerState->eLcdFormat <= LCD_PIXELFORMAT_VYUY))
            {
                ui32SrcSkip = (sRectSrcOn.left & 3);
            }
            else
            {
                ui32SrcSkip = (sRectSrcOn.left & 15);
            }
            if (ui32SrcSkip && sRectSrcOn.right - sRectSrcOn.left)
            {
                ui32DstSkip = ui32SrcSkip * (sRectDstOn.right - sRectDstOn.left) / 
                    (sRectSrcOn.right - sRectSrcOn.left);
            }
            else
            {
                ui32DstSkip = 0;
            }
            sRectSrcOn.left -= ui32SrcSkip;

            WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE0), VPP_TO_LCD_BPP * ui32DstSkip);
            WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE1), VPP_TO_LCD_BPP * ui32DstSkip);

            memcpy(&sVppRectDst , &sRectDstOn, sizeof(RECT));
            sVppRectDst.left -= ui32DstSkip;
            gsVPPFuncTable.pfnSetSize(&sRectSrcOn, &sVppRectDst);  
			if (gsVPPFuncTable.pfnUpdateCoeff)
			{
            	gsVPPFuncTable.pfnUpdateCoeff();
			}
        }
    }
    else
    {
        if (bForceUpdate || iRectSrcOnDiff)
        {
            __LcdSoc_SetDma(eLayer, &sRectSrcOn);
        }
    }

    if (bForceUpdate || iRectDstOnDiff)
    {
        __LcdSoc_SetDstRect(eLayer, &sRectDstOn);
    }

    psLayerState->sRectDstOn = sRectDstOn;
    psLayerState->sRectSrcOn = sRectSrcOn;
}


VOID __LcdSoc_GetPrimarySize(UINT32 *pui32PrimarySize)
{
	UINT w, h, d;	
	LCDSOC_LAYER_STATE *psLayerState = &gsLcdConfig.sLayerState[LCD_PRIMARY];
	d = (psLayerState->eLcdFormat==LCD_PIXELFORMAT_565)?16:32;
	__LcdSoc_GetScreenSize(&w, &h, &gsPanelInfo);
	*pui32PrimarySize = ((BYTE_STRIDE(w, d)*h)+0xfff)&0xfffff000;
}

VOID __LcdSoc_GetFBSize(UINT32 *pui32FBSize)
{
	OSGetFBSize(pui32FBSize);
}

VOID __LcdSoc_PowerDown(VOID)
{
    __LcdSoc_DisableClock();
}

VOID __LcdSoc_Flip(LCD_LAYER eLayer, LCD_FLIP_MODE eFlipMode)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    UINT32 ui32Base = psLayerState->ui32Base;

    if (psLayerState->bNeedVpp)
    {
        if (psLayerState->eFlipMode != eFlipMode)
        {
            BOOL             bTopField;

            psLayerState->eFlipMode = eFlipMode;

            /* disable layer is needed for programming interlace */
            if (psLayerState->bShow)
            {
                __LcdSoc_DisableLayer(eLayer, TRUE);
            }

            if (eFlipMode != LCD_FLIP_FRAME)
            {
                bTopField = (eFlipMode == LCD_FLIP_TOP_FIELD);
                gsVPPFuncTable.pfnSetInterlace(TRUE, VPP_OUTPUT_P_SINGLE, bTopField, bTopField, VPP_DI_VMRI, TRUE, 0);
            }
            else
            {
                gsVPPFuncTable.pfnSetInterlace(FALSE, VPP_OUTPUT_P_SINGLE, TRUE, TRUE, VPP_DI_RESERVED, TRUE, 0);
            }

            if (psLayerState->bShow)
            {
                __LcdSoc_EnableLayer(eLayer);
            }
        }
        gsVPPFuncTable.pfnSetBase(ui32Base);
    }
    else
    {
        UINT8 ui8Bpp = __LcdSoc_EFormatToBpp(psLayerState->eLcdFormat);
        UINT32 ui32Offset = ui8Bpp * (psLayerState->sRectSrc.top*psLayerState->ui32SurfWidth+psLayerState->sRectSrc.left);

        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE0), ui32Base+ui32Offset);
        if (__LcdSoc_IsTVMode(&gsPanelInfo))
        {
            WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_BASE1), ui32Base+ui32Offset+(ui8Bpp*psLayerState->ui32SurfWidth));
        }
    }
}

__inline UINT16 __LcdSoc_MaskTable(UINT8 bAnd, UINT8 bXor)  
{ 
	UINT16 ui16Return   = 0;
	INT i;

    for(i = 0;i< 8;i++)
	{
        ui16Return  |= (bAnd & 1) << (i * 2 + 1 );
		ui16Return  |= (bXor & 1) << (i * 2);
		bAnd    >>= 1;
		bXor    >>= 1;
	}
	return ui16Return;
}

VOID __LcdSoc_GenCursorFIFO(UINT32 *pColor, UINT32 *pMask, INT iMaskStride)
{
    LCDSOC_CURSOR_STATE *psCursorState = &(gsLcdConfig.sCursorState);
    INT iWidth = psCursorState->iWidth; 
    INT iHeight = psCursorState->iHeight;
    UINT32 *pui32Value = &(psCursorState->aui32FIFO[0]);
    UINT32 *pSrc, *pDst, ui32Value;

   	UINT8 *andPtr, *xorPtr; /*input pointer*/
	UINT16 ui16Value, *pui16;
	UINT8 bAnd;
	UINT8 bXor;
    INT i, row, col, j, iDstShift;

    /*Don't support color cursor*/
    if(pColor != NULL)
    {
        LCD_ASSERT(0);
        return;
    }

    if(pMask == NULL)
    {
        /* If pMask == NULL, set the cursor to transparent */
        memset((VOID*)pui32Value, 0xaa, iWidth*iHeight >> 2);
    }
    else
    {
        pui16 = (UINT16*) pui32Value;
        andPtr = (UINT8*) pMask;
        xorPtr = ((UINT8*) pMask) + iHeight*iMaskStride;
        for (row = 0; row < iHeight; row++)
        {
            for (col = 0; col < iWidth/8; col += 2)
            {
                bAnd = andPtr[row * iMaskStride + col];
                bXor = xorPtr[row * iMaskStride + col];
                
                ui16Value = __LcdSoc_MaskTable(bAnd,bXor);

                bAnd = andPtr[row * iMaskStride + col + 1];
                bXor = xorPtr[row * iMaskStride + col + 1];

                *pui16++ = __LcdSoc_MaskTable(bAnd,bXor);
                *pui16++ = ui16Value;
            }
        }
    }

    /* Must clear the rotation fifo since we only OR the bits */
    if (psCursorState->iRotate)
    {
        memset(pui32Value + 256, 0, sizeof(UINT32) * 256);
    }

    switch(psCursorState->iRotate)
    {
    default:
    case 0:
        break;
    case 270:
        pSrc      = (UINT32 *)pui32Value;
        ui32Value = *pSrc++;        

        for(i = 0;i < iHeight;i++)
        {
            pDst      = (UINT32 *)(pui32Value + 256) + iWidth - 1 - (i >> 4);
            iDstShift = (i & 0xf) << 1; 

            for(j = 0;j < iWidth; j++)
            {
                *pDst    |= (ui32Value & 3) << iDstShift;
                ui32Value >>= 2;
                
                if((j & 0xf) == 0xf) 
                {
                    ui32Value = *pSrc ++;
                    pDst   += 0x20 * (iWidth >> 4);
                }
                pDst  -= iWidth >> 4;
            }
        }
        break;

    case 90:
        pSrc = (UINT32 *)pui32Value;
        ui32Value   = *pSrc++;

        for(i = 0;i < iHeight;i++)
        {
            pDst      = (UINT32 *)(pui32Value + 256) + (iWidth >> 4) * ( iWidth - 0x10) + ( i >> 4);
            iDstShift = ((i^0xf) & 0xf) << 1; 

            for(j = 0;j < iWidth; j++)
            {
                *pDst    |= (ui32Value & 3) << iDstShift;
                ui32Value >>= 2;
                
                if((j & 0xf) == 0xf) 
                {
                    ui32Value = *pSrc ++;
                    pDst   -= 0x20 * (iWidth >> 4);
                }
                pDst += iWidth >> 4;
            }
        }
        break;

    case 180:

        pSrc    = (UINT32 *)pui32Value;
        pDst    = (UINT32 *)(pui32Value + 256) + (iHeight >> 4) *  iWidth - 1 ;
        ui32Value = *pSrc++;

        for(i = 0;i < iHeight;i++)
        {
            for(j = 0;j < iWidth; j++)
            {
                *pDst |= (ui32Value & 3) << ((iWidth - j - 1) & 0xf) * 2;
                ui32Value >>= 2;
                
                if((j & 0xf) == 0xf) 
                {
                    ui32Value = *pSrc ++;
                    pDst--;
                }
            }
        }
        break;
    }
}


VOID __LcdSoc_CalcCursorRegion(INT iXPos, INT iYPos, 
    RECT *pRect, INT *piLeftSkip, INT *piTopSkip)
{
    INT32 i32ScreenWidth;
    INT32 i32ScreenHeight;
    INT iCurX=0, iCurY=0;     
    LCDSOC_CURSOR_STATE *psCursorState = &(gsLcdConfig.sCursorState);
    INT iCursorWidth = psCursorState->iWidth;
    INT iCursorHeight = psCursorState->iHeight;

    __LcdSoc_GetScreenSize((UINT32*)&i32ScreenWidth, (UINT32*)&i32ScreenHeight, &gsPanelInfo);

    switch (psCursorState->iRotate)
    {
    default:
    case 0:
        pRect->left = iXPos - psCursorState->iXHot;
        if (pRect->left > i32ScreenWidth - 1)
            pRect->left = i32ScreenWidth - 1;
        
        pRect->right = pRect->left + iCursorWidth;
        if (pRect->right > i32ScreenWidth)
            pRect->right = i32ScreenWidth;
        
        
        pRect->top   = iYPos - psCursorState->iYHot;
        if (pRect->top > i32ScreenHeight - 1)
            pRect->top = i32ScreenHeight - 1;
        
        pRect->bottom = pRect->top + iCursorHeight;
        if (pRect->bottom > i32ScreenHeight)
            pRect->bottom = i32ScreenHeight;
        
        break;

    case 270:
        pRect->top = iXPos - psCursorState->iXHot;
        if (pRect->top > i32ScreenWidth - 1)
            pRect->top = i32ScreenWidth - 1;
        
        pRect->bottom = pRect->top + iCursorWidth;
        if (pRect->bottom > i32ScreenWidth)
            pRect->bottom = i32ScreenWidth;


        pRect->right = iYPos - psCursorState->iYHot;
        if (pRect->right > i32ScreenHeight)
            pRect->right = i32ScreenHeight;       

        pRect->right   = i32ScreenWidth - pRect->right; 
        pRect->left    = pRect->right - iCursorHeight;
        break;

    case 90:
        pRect->bottom  = iXPos - psCursorState->iXHot;
        if (pRect->bottom > i32ScreenWidth)
            pRect->bottom = i32ScreenWidth;
        
        pRect->bottom = i32ScreenHeight - pRect->bottom;
        pRect->top    = pRect->bottom - iCursorWidth;

        pRect->left     = iYPos - psCursorState->iYHot;
        if (pRect->left > i32ScreenHeight - 1)
            pRect->left = i32ScreenHeight - 1;
        
        pRect->right  = pRect->left + iCursorHeight;
        if (pRect->right > i32ScreenHeight)
            pRect->right = i32ScreenHeight;
        break;

    case 180:
        pRect->right   = iXPos - psCursorState->iXHot;
        if (pRect->right > i32ScreenWidth)
            pRect->right = i32ScreenWidth;
        
        pRect->right = i32ScreenWidth - pRect->right;
        pRect->left  = pRect->right - iCursorHeight;

        pRect->bottom = iYPos - psCursorState->iYHot;
        if (pRect->bottom > i32ScreenHeight)
            pRect->bottom = i32ScreenHeight;
        
        pRect->bottom  = i32ScreenHeight - pRect->bottom;
        pRect->top     = pRect->bottom - iCursorHeight;
        break;
    }

    if (pRect->left < 0)
    {
        iCurX  =- pRect->left;
        pRect->left = 0;
    }
    if (pRect->top < 0)
    {
        iCurY  =- pRect->top;
        pRect->top = 0;
    }

    /* TODO: Why we have such requirement? */
    /* The smallest size is 2x2 */
#if 0
    if ((pRect->right - pRect->left) < 2)
    {
        pRect->right = pRect->left + 2;
    }
    if ((pRect->bottom - pRect->top) < 2)
    {
        pRect->bottom = pRect->top + 2;
    }
    if (iCurX == (iCursorWidth-1))
    {
        iCurX --;
    }
    if (iCurY == (iCursorHeight-1))
    {
        iCurY --;
    }
#endif

    *piLeftSkip = iCurX;
    *piTopSkip = iCurY;

}


VOID __LcdSoc_SetCursorRegion(RECT *pRect, INT iLeftSkip, INT iTopSkip)
{
    REG_CUR0_CURRENT_XY reg_CUR0_CURRENT_XY;
    reg_CUR0_CURRENT_XY.DW = 0;
    reg_CUR0_CURRENT_XY.CUR_X = iLeftSkip;
    reg_CUR0_CURRENT_XY.CUR_Y = iTopSkip;
    /* Set hstart, hend */
    switch(gsPanelInfo.eOutFormat)
    {
    case LCD_OUT_8_BIT_RBGRBG:
        WriteLcdRegisterValue(CUR0_HSTART, gsPanelInfo.ui32HStart+(pRect->left*3));
        WriteLcdRegisterValue(CUR0_HEND, gsPanelInfo.ui32HStart+((pRect->right-1)*3));
        break;
    case LCD_OUT_8_BIT_YUV422:
        WriteLcdRegisterValue(CUR0_HSTART, gsPanelInfo.ui32HStart+(pRect->left*2));
        WriteLcdRegisterValue(CUR0_HEND, gsPanelInfo.ui32HStart+((pRect->right-1)*2));
        break;
    default:
        WriteLcdRegisterValue(CUR0_HSTART, gsPanelInfo.ui32HStart+pRect->left);
        WriteLcdRegisterValue(CUR0_HEND, gsPanelInfo.ui32HStart+pRect->right-1);
        break;        
    }
    /* Set vstart, vend */
    WriteLcdRegisterValue(CUR0_VSTART, gsPanelInfo.ui32VStart+pRect->top);    
    WriteLcdRegisterValue(CUR0_VEND, gsPanelInfo.ui32VStart+pRect->bottom-1);
        
    WriteLcdRegisterValue(CUR0_CURRENT_XY, reg_CUR0_CURRENT_XY.DW);
    __LcdSoc_ConfirmCursorSetting();
}


VOID __LcdSoc_SetCursorShape(VOID)
{
    RECT rectVisible;
    INT iLeftSkip=0, iTopSkip=0;
    REG_CUR0_CTRL reg_CUR0_CTRL;
    REG_S0_LAYER_SEL reg_S0_LAYER_SEL;
    UINT32 *pui32Value;
    INT i;
    LCDSOC_CURSOR_STATE *psCursorState = &(gsLcdConfig.sCursorState);

    INT iWidth = psCursorState->iWidth;
    INT iHeight = psCursorState->iHeight;

    if (psCursorState->iRotate == 0)
    {
        pui32Value = &(psCursorState->aui32FIFO[0]);
    }
    else
    {
        pui32Value = &(psCursorState->aui32FIFO[256]);
    }
    
    /*Only support 32*32, 64*64*/
    if(!((iWidth == 32) && (iHeight==32)) && !((iWidth == 64) && (iHeight==64)))
    {
//        LCD_ASSERT(0);
        return;
    }

    reg_S0_LAYER_SEL.DW = ReadLcdRegisterValue(S0_LAYER_SEL);
    reg_S0_LAYER_SEL.LAYER_SEL &= ~(1<<LCD_CURSOR);
    WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);

    reg_CUR0_CTRL.DW = 0;
    
    __LcdSoc_CalcCursorRegion(psCursorState->iXPos, psCursorState->iYPos, 
        &rectVisible, &iLeftSkip, &iTopSkip);
    __LcdSoc_SetCursorRegion(&rectVisible, iLeftSkip, iTopSkip);

    /* Set Cursor Color */
    WriteLcdRegisterValue (CUR0_COLOR0, 0x0);
    WriteLcdRegisterValue (CUR0_COLOR1, 0xffffff);
    WriteLcdRegisterValue (CUR0_ALPHA, 0xff);

    if(psCursorState->iWidth == 32)
    {
        /*256 = 32 * 32 * 2bit / 8*/
        reg_CUR0_CTRL.MODE = LCD_CURSOR_MODE_32x32x2_2_T;
        reg_CUR0_CTRL.SRAM_ADDRST = 1;
        WriteLcdRegisterValue (CUR0_CTRL, reg_CUR0_CTRL.DW);
        for(i = 0;i< 256 ; i += 4)
        {
            WriteLcdRegisterValue (CUR0_FIFODATA + i, *pui32Value ++);
        }
    }
    else
    {
        reg_CUR0_CTRL.MODE = LCD_CURSOR_MODE_64x64x2_2_T;
        reg_CUR0_CTRL.SRAM_ADDRST = 1;
        WriteLcdRegisterValue (CUR0_CTRL, reg_CUR0_CTRL.DW);
        /*1024 = 64 * 64 * 2bit / 8*/
        for(i = 0;i< 1024 ; i += 4)
        {
            WriteLcdRegisterValue (CUR0_FIFODATA + i, *pui32Value ++);
        }
    }
    reg_CUR0_CTRL.SRAM_ADDRST = 0;
    reg_CUR0_CTRL.SETTING_VALID = 1;    
    WriteLcdRegisterValue (CUR0_CTRL, reg_CUR0_CTRL.DW);

    if (psCursorState->bShow)
    {
        reg_S0_LAYER_SEL.LAYER_SEL |= (1<<LCD_CURSOR);
        WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);
    }
}


VOID __LcdSoc_MoveCursor(VOID)
{
    RECT rectVisible;
    INT iLeftSkip=0, iTopSkip=0;
    LCDSOC_CURSOR_STATE *psCursorState = &(gsLcdConfig.sCursorState);
    INT iXPos = psCursorState->iXPos;
    INT iYPos = psCursorState->iYPos;
    REG_S0_LAYER_SEL reg_S0_LAYER_SEL;

    /*QUESTION: may not disable cursor when move pointer*/
    /*Disable cursor*/
    reg_S0_LAYER_SEL.DW = ReadLcdRegisterValue(S0_LAYER_SEL);
    reg_S0_LAYER_SEL.LAYER_SEL &= ~(1<<LCD_CURSOR);
    WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);

    if(psCursorState->bShow && (psCursorState->iWidth>0) && (psCursorState->iHeight>0))
    {
        /*Calculate visible region of cursor*/
        __LcdSoc_CalcCursorRegion(iXPos, iYPos, &rectVisible, &iLeftSkip, &iTopSkip);

        /*Calculate visible region of cursor*/
        __LcdSoc_SetCursorRegion(&rectVisible, iLeftSkip, iTopSkip);

        /*Enable cursor*/
        reg_S0_LAYER_SEL.LAYER_SEL |= (1<<LCD_CURSOR);
        WriteLcdRegisterValue(S0_LAYER_SEL, reg_S0_LAYER_SEL.DW);
    }
}

VOID __LcdSoc_SetGlobalAlpha(LCD_LAYER eLayer)
{
    REG_L0_ALPHA reg_L0_ALPHA;
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    reg_L0_ALPHA.DW = 0;
    reg_L0_ALPHA.ALPHA_VAL = psLayerState->ui8Alpha;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_ALPHA), reg_L0_ALPHA.DW);
}

VOID __LcdSoc_SetAlphaProperty(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    REG_L0_CTRL reg_L0_CTRL;
    reg_L0_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL));
    reg_L0_CTRL.GLOBAL_ALPHA = psLayerState->bGlobalAlpha?1:0;
    if (psLayerState->eLcdFormat == LCD_PIXELFORMAT_8888)
    {
        reg_L0_CTRL.PREMULTI_ALPHA = psLayerState->bPremultiAlpha?1:0;
        reg_L0_CTRL.SOURCE_ALPHA = psLayerState->bSourceAlpha?1:0;
    }
    else
    {
        /* Spec require following setting for non-ARGB format */
        reg_L0_CTRL.PREMULTI_ALPHA = 1;
        reg_L0_CTRL.SOURCE_ALPHA = 0;
    }
    reg_L0_CTRL.CONFIRM = 0;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_L0_CTRL.DW);
}

VOID __LcdSoc_SetColorKey(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_PIXELFORMAT eFormat = psLayerState->eLcdFormat;
    LCD_PIXELFORMAT ePrimaryFormat = gsLcdConfig.sLayerState[E_PRIMARY].eLcdFormat;
    BOOL bPrimaryReplicate = gsLcdConfig.sLayerState[E_PRIMARY].bReplicate;
    REG_L0_CTRL reg_Lx_CTRL;

    reg_Lx_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL));

    if (psLayerState->bCKeyOn)
    {
        reg_Lx_CTRL.SRC_CKEY_EN = 1;
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CKEYB_SRC), __LcdSoc_CKValue(eFormat, psLayerState->bReplicate, psLayerState->ui32CKHigh));
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CKEYS_SRC), __LcdSoc_CKValue(eFormat, psLayerState->bReplicate, psLayerState->ui32CKLow));
    }
    else
    {
        reg_Lx_CTRL.SRC_CKEY_EN = 0;
    }
    if (psLayerState->bCKeyDstOn)
    {
        reg_Lx_CTRL.DST_CKEY_EN = 1;
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CKEYB_DST), __LcdSoc_CKValue(ePrimaryFormat, bPrimaryReplicate, psLayerState->ui32CKDstHigh));
        WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CKEYS_DST), __LcdSoc_CKValue(ePrimaryFormat, bPrimaryReplicate, psLayerState->ui32CKDstLow));
    }
    else
    {
        reg_Lx_CTRL.DST_CKEY_EN = 0;
    }
    reg_Lx_CTRL.CONFIRM = 0;
    WriteLcdRegisterValue(REG_OFFSET(eLayer, L0_CTRL), reg_Lx_CTRL.DW);
}

VOID __LcdSoc_SetTopLayer(VOID)
{
    REG_S0_DISP_MODE reg_S0_DISP_MODE;
    reg_S0_DISP_MODE.DW = ReadLcdRegisterValue(S0_DISP_MODE);
    reg_S0_DISP_MODE.TOP_LAYER = gsLcdConfig.eTopLayer;
    reg_S0_DISP_MODE.FRAME_VALID = 1; /*FRAME_VALID will be 0 if screen_en is 0 even write 1 to this bit*/
    WriteLcdRegisterValue(S0_DISP_MODE, reg_S0_DISP_MODE.DW);
}

VOID __LcdSoc_ResetLayerState(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);

    /* Set default layer state here */
    memset(psLayerState, 0, sizeof(LCDSOC_LAYER_STATE));
    psLayerState->bReplicate = LCD_DEFAULT_REPLICATE;
    psLayerState->bPremultiAlpha = LCD_DEFAULT_PREMULTI_ALPHA;
}

VOID __LcdSoc_SetGammaRamp(VOID)
{
    INT                 i;
    UINT32              *pui32Value;
    REG_S0_DISP_MODE    reg_S0_DISP_MODE;

    reg_S0_DISP_MODE.DW = ReadLcdRegisterValue(S0_DISP_MODE);
    reg_S0_DISP_MODE.GAMMA_COR_EN = 0;
    reg_S0_DISP_MODE.FRAME_VALID = 1; /*FRAME_VALID will be 0 if screen_en is 0 even write 1 to this bit*/
    WriteLcdRegisterValue(S0_DISP_MODE, reg_S0_DISP_MODE.DW);
    pui32Value = (UINT32 *)&gsLcdConfig.aui8Gamma[0];
    for (i=0; i < 256 * 3; i += 4)
    {
        WriteLcdRegisterValue(S0_GAMMAFIFO_R + i, pui32Value[i>>2]);
    }        
    reg_S0_DISP_MODE.GAMMA_COR_EN = 1;
    WriteLcdRegisterValue(S0_DISP_MODE, reg_S0_DISP_MODE.DW);
}

VOID __LcdSoc_SetInterrupt(VOID)
{
    /* Clear interrupt firstly before enable */
    WriteLcdRegisterValue(INT_CTRL_STATUS, gsLcdConfig.ui32IntState);
    WriteLcdRegisterValue(INT_MASK, gsLcdConfig.ui32IntState);	
}

BOOL LcdSoc_ChangeMode(LCD_PANEL_INFO *psPanel)
{
	UINT32 ui32PrimBase = gsLcdConfig.sLayerState[psPanel->eLayer].ui32Base;
	LCD_PIXELFORMAT ui32Format = gsLcdConfig.sLayerState[psPanel->eLayer].eLcdFormat;

	if (psPanel->pfnPrePowerDown)
		psPanel->pfnPrePowerDown();
	__LcdSoc_PowerDown();
	if (psPanel->pfnPostPowerDown)
		psPanel->pfnPostPowerDown();

	if (psPanel->pfnPrePowerUp)
		psPanel->pfnPrePowerUp();
	__LcdSoc_PowerUp(ui32PrimBase, ui32Format, psPanel);
	if (psPanel->pfnPostPowerUp)
		psPanel->pfnPostPowerUp();

	gsPanelInfo = *psPanel;
	return TRUE;
}

BOOL LcdSoc_Initialize(VOID *pLcdRegs, 
						VOID *pVppRegs,
						UINT32 ui32PrimBase, 
						UINT32 ui32BitPerPixel,
						LCD_PANEL_INFO *psPanel)
{
    LCDSOC_LAYER_STATE *psLayerState;
    REG_S0_LAYER_SEL reg_S0_LAYER_SEL;
    INT i;
	UINT w, h;
	BOOL bEnabledBefore = TRUE;
	BOOL bShow = TRUE;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));

	if (pLcdRegs)
	{
		gpui8LcdRegs = pLcdRegs;
	}
	else
	{
	    gpui8LcdRegs = (volatile UINT8 *)OSMapLcdRegs();
		if (!gpui8LcdRegs)
		{
			LCD_MSG(("LcdSoc_Initialize: OSMapLcdRegs fail\n"));
			return FALSE;
		}
	}

	if (psPanel)
	{
		gsPanelInfo = *psPanel;
	}
	else
	{
		OSGetPanelInfo(&gsPanelInfo);
	}

	/* Enable clock if LCD not boot up in oal */
	__LcdSoc_EnableClock();

    reg_S0_LAYER_SEL.DW = ReadLcdRegisterValue(S0_LAYER_SEL);
    if (!(reg_S0_LAYER_SEL.DW & (0x1<<LCD_PRIMARY)))
    {
    	LCD_MSG(("%s: bEnabedBefore == FALSE\r\n", __FUNCTION__));
    	bEnabledBefore = FALSE;
		if (!ui32PrimBase || !ui32BitPerPixel)
		{
			bShow = FALSE;
			gsPanelInfo.pfnReset();

			if (gsPanelInfo.pfnPrePowerUp)
				gsPanelInfo.pfnPrePowerUp();

			__LcdSoc_ConfigScreen(&gsPanelInfo);

			if (gsPanelInfo.pfnPostPowerUp)
				gsPanelInfo.pfnPostPowerUp();
		}
		else
		{
	        LCD_BootUp((VOID*)gpui8LcdRegs, 
							ui32PrimBase, 
							ui32BitPerPixel, 
							&gsPanelInfo);
		}
    }
	else
	{
		REG_L0_CTRL reg_L0_CTRL;
		reg_L0_CTRL.DW = ReadLcdRegisterValue(REG_OFFSET(LCD_PRIMARY, L0_CTRL));

		/* Driver should keep consistant with UBOOT */
		if (reg_L0_CTRL.BPP == E_LO_CTRL_BPP_RGB565)
		{
			ui32BitPerPixel = 16;
		}
		else
		{
			ui32BitPerPixel = 32;
		}
		ui32PrimBase = ReadLcdRegisterValue(REG_OFFSET(LCD_PRIMARY, L0_BASE0));
	}

    memset(&gsLcdConfig, 0, sizeof(gsLcdConfig));

	if (bShow)
	{
		__LcdSoc_GetScreenSize(&w, &h, &gsPanelInfo);
	    psLayerState = &gsLcdConfig.sLayerState[LCD_PRIMARY];
	    psLayerState->ui32Base = ui32PrimBase;
	    psLayerState->ui32SurfWidth = w;
	    psLayerState->ui32SurfHeight = h;
	    psLayerState->eLcdFormat = (ui32BitPerPixel==16)?(LCD_PIXELFORMAT_565):(LCD_PIXELFORMAT_8888);
	    psLayerState->sRectSrc.left = psLayerState->sRectDst.left = 0;
	    psLayerState->sRectSrc.top = psLayerState->sRectDst.top = 0;
	    psLayerState->sRectSrc.right = psLayerState->sRectDst.right = w;
	    psLayerState->sRectSrc.bottom = psLayerState->sRectDst.bottom = h;
	    psLayerState->bReplicate = LCD_DEFAULT_REPLICATE;
	    if (psLayerState->eLcdFormat == LCD_PIXELFORMAT_8888)
		psLayerState->bSourceAlpha = 1;
	    else
		psLayerState->bSourceAlpha = 0;
	    psLayerState->bPremultiAlpha = LCD_DEFAULT_PREMULTI_ALPHA;
	    psLayerState->bInUse = TRUE;    
	    psLayerState->bShow = TRUE;		
	    __LcdSoc_GetFBSize(&gsLcdConfig.ui32FBSize);
	}

	if (OSLoadVpp(&gsLcdConfig.hVppHandle, &gsVPPFuncTable))
	{
		if (pVppRegs)
		{
			gsVPPFuncTable.pfnInitialize(pVppRegs);
		}
		else
		{
			gsVPPFuncTable.pfnInitialize(OSGetVppRegs());
		}
	}

	gsLcdConfig.eTopLayer = gsPanelInfo.eMaxLayer;
    gsLcdConfig.bGammaEnable = FALSE;
    for (i = 0; i < 256; i++)
    {
        gsLcdConfig.aui8Gamma[i] = i;
        gsLcdConfig.aui8Gamma[256 + i] = i;
        gsLcdConfig.aui8Gamma[512 + i] = i;
    }
    for (i = LCD_OVERLAY_1; i <= gsPanelInfo.eMaxLayer; i++)
    {
        psLayerState = &gsLcdConfig.sLayerState[i];
        psLayerState->i32Brightness = 0;
        psLayerState->i32Contrast = 128;
        psLayerState->i32Hue = 0;
        psLayerState->i32Saturation = 128;
    }
	return bEnabledBefore;
}

VOID LcdSoc_Terminate(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (gsLcdConfig.hVppHandle)
    {
        gsVPPFuncTable.pfnTerminate();
		OSUnloadVpp(gsLcdConfig.hVppHandle);
        gsLcdConfig.hVppHandle = NULL;
    }

	if (gsPanelInfo.pfnPrePowerDown)
		gsPanelInfo.pfnPrePowerDown();
	__LcdSoc_PowerDown();
	if (gsPanelInfo.pfnPostPowerDown)
		gsPanelInfo.pfnPostPowerDown();

	OSUnmapLcdRegs();
}


VOID LcdSoc_Sleep(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));

    if (gsPanelInfo.pfnPrePowerDown)
        gsPanelInfo.pfnPrePowerDown();

    __LcdSoc_DisableClock();
    if (gsLcdConfig.hVppHandle)
    {
        gsVPPFuncTable.pfnSleep();
    }

    if (gsPanelInfo.pfnPostPowerDown)
        gsPanelInfo.pfnPostPowerDown();
}

BOOL LcdSoc_Wakeup(VOID)
{
    LCD_LAYER eLayer;
	REG_S0_LAYER_SEL reg_S0_LAYER_SEL;
	BOOL bEnabledBefore = TRUE;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));    

    __LcdSoc_EnableClock();

    reg_S0_LAYER_SEL.DW = ReadLcdRegisterValue(S0_LAYER_SEL);
    
/* accel-hiberation/resume, VCC will not enable. suspend/resume ok.
 * reason:when hiberation, FB will disable VCC via gpio ,
 * and pm suspend will save gpio. after resume back, uboot will init lcd.
 * later pm resume will restore saved gpio, this will disable VCC
 * and FB won't power on lcd since uboot have done, so add workaround here.
 */
    if (gsPanelInfo.pfnPrePowerUp)
        gsPanelInfo.pfnPrePowerUp();

    if (!(reg_S0_LAYER_SEL.DW & (0x1<<LCD_PRIMARY)))
    {        
    	bEnabledBefore = FALSE;
    	
	__LcdSoc_ConfigScreen(&gsPanelInfo);
    }
    
    if (gsLcdConfig.hVppHandle)
    {
        gsVPPFuncTable.pfnWakeup();
    }

	__LcdSoc_SetInterrupt();

    for (eLayer=LCD_PRIMARY; eLayer<=gsPanelInfo.eMaxLayer; eLayer++)
    {
        LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
        if (psLayerState->bShow)
        {
            __LcdSoc_SetParameters(eLayer);
        }
    }
    
    if (gsLcdConfig.sCursorState.bShow)
    {
        __LcdSoc_SetCursorShape();
        
        __LcdSoc_MoveCursor();
    }
    
    if (gsLcdConfig.eTopLayer != gsPanelInfo.eMaxLayer)
    {
        __LcdSoc_SetTopLayer();
    }
    
    if (gsLcdConfig.bGammaEnable)
    {
        __LcdSoc_SetGammaRamp();
    }
    
	/* Fix me */
	/* tmp solution to fix hibernation cold boot taishan 8" bl issue */
	/* if (!bEnabledBefore) */
    {
        if (gsPanelInfo.pfnPostPowerUp)
            gsPanelInfo.pfnPostPowerUp();
    }

	return bEnabledBefore;
}



VOID LcdSoc_GetScanLine(LCD_GETSCANLINE_DATA *pData)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    *(pData->pScanLine)= ReadLcdRegisterValue(S0_VCOUNT)-gsPanelInfo.ui32VStart;
}

VOID LcdSoc_WaitForVBlank(LCD_WAITFORVBLANK_DATA *pVBlankData)
{
    INT times = 3000000; /* ~= 18ms */
   	UINT32 ui32Line, ui32ScnVEnd;
    BOOL bInVB;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    do
    {
        ui32Line = ReadLcdRegisterValue(S0_VCOUNT);
        ui32ScnVEnd = ReadLcdRegisterValue(S0_ACT_VEND);
        bInVB = (ui32Line >= ui32ScnVEnd) && 
            (ui32Line < gsPanelInfo.ui32VStart);
        times--;
    }while((times > 0) && (bInVB == pVBlankData->bBlockBegin));
    return;
}


VOID LcdSoc_ClearInterrupt(LCD_INTERRUPT_TYPE eType)
{
//    LCD_ENTRY(("%s\r\n",__FUNCTION__));

	if (eType == LCD_INTERRUPT_ALL)
	{
	    WriteLcdRegisterValue(INT_CTRL_STATUS, 0xFFFFFFFF);
	}
	else
	{
		REG_INT_CTRL_STATUS reg_INT_CTRL_STATUS;
	    reg_INT_CTRL_STATUS.DW = (1<<eType);
	    WriteLcdRegisterValue(INT_CTRL_STATUS, reg_INT_CTRL_STATUS.DW);
	}
}
VOID LcdSoc_EnableInterrupt(LCD_INTERRUPT_TYPE eType)
{
//    LCD_ENTRY(("%s\r\n",__FUNCTION__));
	if (eType == LCD_INTERRUPT_ALL)
	{
		gsLcdConfig.ui32IntState = 0xFFFFFFFF;
	}
	else
	{
		gsLcdConfig.ui32IntState |= (1<<eType);
	}
	__LcdSoc_SetInterrupt();
}
VOID LcdSoc_DisableInterrupt(LCD_INTERRUPT_TYPE eType)
{
//    LCD_ENTRY(("%s\r\n",__FUNCTION__));
	if (eType == LCD_INTERRUPT_ALL)
	{
		gsLcdConfig.ui32IntState = 0x0;
	}
	else
	{
		gsLcdConfig.ui32IntState &= (~(1<<eType));
	}
	__LcdSoc_SetInterrupt();
}

UINT32 LcdSoc_IsInterrupted(LCD_INTERRUPT_TYPE eType)
{
    REG_INT_CTRL_STATUS reg_INT_CTRL_STATUS;
	REG_INT_MASK reg_INT_MASK;
	UINT32 ui32Status;
//    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    reg_INT_CTRL_STATUS.DW = ReadLcdRegisterValue(INT_CTRL_STATUS);
	reg_INT_MASK.DW = ReadLcdRegisterValue(INT_MASK);
#if 0		//for underflow test
	//here is for underflow test
	//for continue vsync happen
	{
		static DWORD LastInterrupt=0,Count=0;
		if (reg_INT_CTRL_STATUS.S0_LINE_INT_INT)
		{
			LcdSoc_ClearInterrupt(LCD_INTERRUPT_VSYNC);
			LcdSoc_EnableInterrupt(LCD_INTERRUPT_VSYNC);
		}
		//for underflow judge
		if (reg_INT_CTRL_STATUS.DW&0xf3c0)
		{
			if (LastInterrupt!=(reg_INT_CTRL_STATUS.DW&0xf3c0)||Count>1000)
			{
				RETAILMSG(1,(TEXT("I=%x\r\n"),reg_INT_CTRL_STATUS.DW));
				LastInterrupt=reg_INT_CTRL_STATUS.DW&0xf3c0;
				Count=0;
			}
			else
				Count++;
	
			//clear other interrrupts
			WriteLcdRegisterValue(INT_CTRL_STATUS, 0xf3c0);
		}
	}
#endif	

	ui32Status = reg_INT_CTRL_STATUS.DW & reg_INT_MASK.DW;
	if (eType == LCD_INTERRUPT_ALL)
	{
		return ui32Status;
	}
	else
	{
		return (ui32Status & (1<<eType));
	}
}

VOID LcdSoc_GetMode(LCD_GETMODE_DATA *pDisplayMode)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[LCD_PRIMARY]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if(pDisplayMode)
    {
        pDisplayMode->eFormat = psLayerState->eLcdFormat;
        __LcdSoc_GetScreenSize(&pDisplayMode->ui32Width, &pDisplayMode->ui32Height, &gsPanelInfo);
        pDisplayMode->ui32ByteStride = 
            psLayerState->ui32SurfWidth * 
            (__LcdSoc_EFormatToBpp(pDisplayMode->eFormat));
        pDisplayMode->ui32RefreshHZ = LCD_DISPLAY_FREQUENCY;
    }
}

VOID LcdSoc_GetVidMem(LCD_GETVIDMEM_DATA *pData)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    pData->ui32Size = gsLcdConfig.ui32FBSize;

    __LcdSoc_GetPrimarySize(&pData->ui32PrimarySize);
	OSGetFBBase(pData);
}

LCD_LAYER LcdSoc_AllocOverlay(LCD_ALLOCOVERLAY_DATA *pData)
{
    LCDSOC_LAYER_STATE *psLayerState;
    LCD_LAYER eLayer, eRetLayer;
    BOOL      bNeedVpp = FALSE;

    LCD_ENTRY(("+%s: eLayer:%d eFormat:%d w:%d h:%d\r\n",
		__FUNCTION__,
		pData->eLayer, 
		pData->eLcdFormat, 
		pData->i32Width, 
		pData->i32Height));

    if((pData->eLayer != LCD_LAYER_UNKNOWN) && 
            ((pData->eLayer < LCD_OVERLAY_1) || (pData->eLayer > gsPanelInfo.eMaxLayer)))
    {
        LCD_ASSERT(0);
        LCD_MSG(("Wrong layer to allocate"));
        return LCD_LAYER_UNKNOWN;
    }

    /* if support the format */
    switch (pData->eLcdFormat)
    {
    case LCD_PIXELFORMAT_565:
    case LCD_PIXELFORMAT_556:
    case LCD_PIXELFORMAT_655:

    case LCD_PIXELFORMAT_BGRX_8880:
    case LCD_PIXELFORMAT_8888:
        pData->i32WStrideByte = ((__LcdSoc_EFormatToBpp(pData->eLcdFormat) *
            pData->i32Width + 7) / 8) * 8;
        pData->i32HStrideByte = pData->i32WStrideByte * pData->i32Height;
        pData->i32WStridePixel = pData->i32Width;
        pData->i32HStridePixel = pData->i32HStrideByte / pData->i32WStrideByte;
        break;
    case LCD_PIXELFORMAT_YUYV:
    case LCD_PIXELFORMAT_UYVY:
    case LCD_PIXELFORMAT_YUY2:
    case LCD_PIXELFORMAT_YUNV:
    case LCD_PIXELFORMAT_YVYU:
    case LCD_PIXELFORMAT_UYNV:
    case LCD_PIXELFORMAT_VYUY:

    case LCD_PIXELFORMAT_IMC1:
    case LCD_PIXELFORMAT_IMC3:
    case LCD_PIXELFORMAT_YV12:
    case LCD_PIXELFORMAT_I420:
    case LCD_PIXELFORMAT_UYVI:
    case LCD_PIXELFORMAT_NV12:
    case LCD_PIXELFORMAT_NV21:
        if (!gsLcdConfig.hVppHandle)
        {
            pData->eLcdFormat = LCD_PIXELFORMAT_UNKNOWN;
	    return LCD_LAYER_UNKNOWN;
        }
        if (gsVPPFuncTable.pfnAllocOverlay(pData))
        {
            bNeedVpp = TRUE;
        }
        else
        {
            return LCD_LAYER_UNKNOWN;
        }
        break;
    default:
        pData->eLcdFormat = LCD_PIXELFORMAT_UNKNOWN;
        return LCD_LAYER_UNKNOWN;
    }

    /* if support the dimension */
    if ((pData->i32Width > LCD_MAX_OVERLAY_WIDTH) ||
        (pData->i32Height > LCD_MAX_OVERLAY_HEIGHT) ||
        (pData->eLcdFormat == LCD_PIXELFORMAT_UNKNOWN))
    {
        return LCD_LAYER_UNKNOWN;
    }  


    eRetLayer = LCD_LAYER_UNKNOWN;
    if(pData->eLayer == LCD_LAYER_UNKNOWN)
    {
        for (eLayer = LCD_OVERLAY_1; eLayer <= gsPanelInfo.eMaxLayer; eLayer++)
        {
            psLayerState =  &(gsLcdConfig.sLayerState[eLayer]);
            if (!psLayerState->bInUse)
            {
                eRetLayer = eLayer;
                break;
            }
        }
    }
    else
    {
        psLayerState =  &(gsLcdConfig.sLayerState[pData->eLayer]);
        if (!psLayerState->bInUse)
        {
            eRetLayer = pData->eLayer;
        }    
    }

    if (eRetLayer != LCD_LAYER_UNKNOWN)
    {
        __LcdSoc_ResetLayerState(eRetLayer);
        psLayerState =  &(gsLcdConfig.sLayerState[eRetLayer]);
        psLayerState->bInUse = TRUE;
        psLayerState->bShow = FALSE;
        psLayerState->bNeedVpp = bNeedVpp;
        psLayerState->eLcdFormat = pData->eLcdFormat;
        psLayerState->ui32SurfWidth = pData->i32WStridePixel;
        psLayerState->ui32SurfHeight = pData->i32HStridePixel;
        psLayerState->sRectSrc.left = psLayerState->sRectSrc.top = 0;
        psLayerState->sRectSrc.right = pData->i32Width;
        psLayerState->sRectSrc.bottom = pData->i32Height;
    }

    LCD_ENTRY(("-%s: eRetLayer:%d wstride_pixel:%d hstride_pixel:%d\r\n",
		__FUNCTION__,
		eRetLayer, 
		pData->i32WStridePixel, 
		pData->i32HStridePixel));
    return eRetLayer;
}

VOID LcdSoc_FreeOverlay(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if(psLayerState->bInUse && psLayerState->bShow)
    {
        LcdSoc_HideOverlay(eLayer);
    }
    psLayerState->bInUse = FALSE;
    return;
}

VOID LcdSoc_ShowOverlay(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s eLayer=%d\r\n",__FUNCTION__, eLayer));

	if (!psLayerState->bShow &&
		(psLayerState->sRectDst.right - psLayerState->sRectDst.left != 0) &&
		(psLayerState->sRectDst.bottom - psLayerState->sRectDst.top != 0))
	{
		psLayerState->bShow = TRUE;
		__LcdSoc_SetParameters(eLayer);
	}
}

VOID LcdSoc_PrintParameters(LCD_SETPARAMS_DATA *pData)
{
	LCD_MSG(("LCD layer %d parameters:\n", pData->eLayer));
	LCD_MSG(("eLcdFormat=%d\n", pData->eLcdFormat));
	LCD_MSG(("i32SurfWidth=%d\n", pData->i32SurfWidth));
	LCD_MSG(("i32SurfHeight=%d\n", pData->i32SurfHeight));
	LCD_MSG(("sRectSrc=%d %d %d %d\n", pData->sRectSrc.left, pData->sRectSrc.top, 
		pData->sRectSrc.right, pData->sRectSrc.bottom));
	LCD_MSG(("sRectDst=%d %d %d %d\n", pData->sRectDst.left, pData->sRectDst.top, 
		pData->sRectDst.right, pData->sRectDst.bottom));
	LCD_MSG(("ui32Base=0x%x\n", pData->ui32Base));
	LCD_MSG(("bCKeyOn=%d\n", pData->bCKeyOn));
	LCD_MSG(("ui32CKLow=0x%x\n", pData->ui32CKLow));
	LCD_MSG(("ui32CKHigh=0x%x\n", pData->ui32CKHigh));
	LCD_MSG(("bCKeyDstOn=%d\n", pData->bCKeyDstOn));
	LCD_MSG(("ui32CKDstLow=0x%x\n", pData->ui32CKDstLow));
	LCD_MSG(("ui32CKDstHigh=0x%x\n", pData->ui32CKDstHigh));
	LCD_MSG(("bGlobalAlpha=%d\n", pData->bGlobalAlpha));
	LCD_MSG(("bSourceAlpha=%d\n", pData->bSourceAlpha));
	LCD_MSG(("bPremultiAlpha=%d\n", pData->bPremultiAlpha));
	LCD_MSG(("ui8Alpha=0x%x\n", pData->ui8Alpha));
	
}

BOOL LcdSoc_SetParameters(LCD_SETPARAMS_DATA *pData)
{
    LCD_LAYER eLayer = pData->eLayer;
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));

    psLayerState->bNeedVpp = __LcdSoc_NeedVpp(pData->eLcdFormat);
    if ((gsLcdConfig.hVppHandle==NULL) && psLayerState->bNeedVpp)
    {
        LCD_ASSERT(0);
		return FALSE;
    }

    psLayerState->eLcdFormat = pData->eLcdFormat;
    psLayerState->ui32SurfWidth = pData->i32SurfWidth;
    psLayerState->ui32SurfHeight = pData->i32SurfHeight;
    psLayerState->sRectSrc = pData->sRectSrc;
    psLayerState->sRectDst = pData->sRectDst;
    psLayerState->ui32Base = pData->ui32Base;
    psLayerState->bCKeyOn = pData->bCKeyOn;
    psLayerState->ui32CKLow = pData->ui32CKLow;
    psLayerState->ui32CKHigh = pData->ui32CKHigh;
    psLayerState->bCKeyDstOn = pData->bCKeyDstOn;
    psLayerState->ui32CKDstLow = pData->ui32CKDstLow;
    psLayerState->ui32CKDstHigh = pData->ui32CKDstHigh;
    psLayerState->bGlobalAlpha = pData->bGlobalAlpha;
    psLayerState->bSourceAlpha = pData->bSourceAlpha;
    psLayerState->bPremultiAlpha = pData->bPremultiAlpha;
    psLayerState->ui8Alpha = pData->ui8Alpha;
    return __LcdSoc_SetParameters(eLayer);
}

VOID LcdSoc_GetParameters(LCD_SETPARAMS_DATA *pData)
{
    LCD_LAYER eLayer = pData->eLayer;
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    
    if ((gsLcdConfig.hVppHandle==NULL) && (__LcdSoc_NeedVpp(pData->eLcdFormat)))
    {
        LCD_ASSERT(0);
    }

    pData->eLcdFormat = psLayerState->eLcdFormat;
    pData->i32SurfWidth = psLayerState->ui32SurfWidth;
    pData->i32SurfHeight = psLayerState->ui32SurfHeight;
    pData->sRectSrc = psLayerState->sRectSrc;
    pData->sRectDst = psLayerState->sRectDst;
    pData->ui32Base = psLayerState->ui32Base;
    pData->bCKeyOn = psLayerState->bCKeyOn;
    pData->ui32CKLow = psLayerState->ui32CKLow;
    pData->ui32CKHigh = psLayerState->ui32CKHigh;
    pData->bCKeyDstOn = psLayerState->bCKeyDstOn;
    pData->ui32CKDstLow = psLayerState->ui32CKDstLow;
    pData->ui32CKDstHigh = psLayerState->ui32CKDstHigh;
    pData->bGlobalAlpha = psLayerState->bGlobalAlpha;
    pData->bSourceAlpha = psLayerState->bSourceAlpha;
    pData->bPremultiAlpha = psLayerState->bPremultiAlpha;
    pData->ui8Alpha = psLayerState->ui8Alpha;
}


VOID LcdSoc_HideOverlay(LCD_LAYER eLayer)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (psLayerState->bShow)
    {
        psLayerState->bShow = FALSE;
        __LcdSoc_DisableLayer(eLayer, FALSE);

        if (psLayerState->bNeedVpp)
        {
            gsVPPFuncTable.pfnUnlock();
        }
    }
}

VOID LcdSoc_PanDisplay(LCD_LAYER eLayer, INT x, INT y)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
	INT w,h;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
	w = psLayerState->sRectSrc.right - psLayerState->sRectSrc.left;
	h = psLayerState->sRectSrc.bottom - psLayerState->sRectSrc.top;
	psLayerState->sRectSrc.left = x;
	psLayerState->sRectSrc.right = x + w;
	psLayerState->sRectSrc.top = y;
	psLayerState->sRectSrc.bottom = y + h;
	__LcdSoc_Flip(eLayer, LCD_FLIP_FRAME);
	__LcdSoc_ConfirmLayerSetting(eLayer);
}

VOID LcdSoc_SetOverlayPos(LCD_LAYER eLayer, RECT *pSrc, RECT *pDst)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (pDst)
    {
        psLayerState->sRectDst = *pDst;
    }
    
    if (pSrc)
    {
        psLayerState->sRectSrc = *pSrc;
    }
    if (psLayerState->bShow)
    {
        /* Only update src, dst related parameters only when they change */
        __LcdSoc_SetSize(eLayer, FALSE);
        __LcdSoc_ConfirmLayerSetting(eLayer);
    }
}

VOID LcdSoc_SetGlobalAlpha(LCD_LAYER eLayer, UINT8 ui8Alpha)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psLayerState->ui8Alpha = ui8Alpha;
    if (psLayerState->bShow)
    {
        __LcdSoc_SetGlobalAlpha(eLayer);
        __LcdSoc_ConfirmLayerSetting(eLayer);
    }
}

VOID LcdSoc_SetAlphaProperty(LCD_LAYER eLayer, BOOL bPremulti, BOOL bGlobal, BOOL bSource)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psLayerState->bPremultiAlpha = bPremulti;
    psLayerState->bGlobalAlpha = bGlobal;
    psLayerState->bSourceAlpha = bSource;
    if (psLayerState->bShow)
    {
        __LcdSoc_SetAlphaProperty(eLayer);
        __LcdSoc_ConfirmLayerSetting(eLayer);
    }
}

VOID LcdSoc_SetSrcCKey(LCD_LAYER eLayer, BOOL bOn, UINT32 ui32High, UINT32 ui32Low)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psLayerState->bCKeyOn = bOn;
    psLayerState->ui32CKHigh = ui32High;
    psLayerState->ui32CKLow = ui32Low;
    if (psLayerState->bShow)
    {
        __LcdSoc_SetColorKey(eLayer);
        __LcdSoc_ConfirmLayerSetting(eLayer);
    }
}

VOID LcdSoc_SetDstCKey(LCD_LAYER eLayer, BOOL bOn, UINT32 ui32High, UINT32 ui32Low)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psLayerState->bCKeyDstOn = bOn;
    psLayerState->ui32CKDstHigh = ui32High;
    psLayerState->ui32CKDstLow = ui32Low;
    if (psLayerState->bShow)
    {
        __LcdSoc_SetColorKey(eLayer);		
        __LcdSoc_ConfirmLayerSetting(eLayer);
    }
}

LCD_LAYER LcdSoc_GetTopLayer(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    return gsLcdConfig.eTopLayer;    
}

VOID LcdSoc_SetTopLayer(LCD_LAYER eLayer)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsLcdConfig.eTopLayer = eLayer;    
    __LcdSoc_SetTopLayer();        
}


VOID LcdSoc_FlipOverlay(LCD_LAYER eLayer, UINT32 ui32Base, LCD_FLIP_MODE eField)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psLayerState->ui32Base = ui32Base;
    if (psLayerState->bShow)
    {
        __LcdSoc_Flip(eLayer, eField);
        __LcdSoc_ConfirmLayerSetting(eLayer);
    }
}

VOID LcdSoc_SetCursorShape(UINT32 *pMask, INT iMaskStride, 
    UINT32 *pColor, INT iXHot, INT iYHot, INT iWidth, INT iHeight)
{
    LCDSOC_CURSOR_STATE *psCursorState = &(gsLcdConfig.sCursorState);
//    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psCursorState->iXHot = iXHot;
    psCursorState->iYHot = iYHot;
    psCursorState->iWidth = iWidth;
    psCursorState->iHeight = iHeight;
    __LcdSoc_GenCursorFIFO(pColor, pMask, iMaskStride);
    __LcdSoc_SetCursorShape();
}
VOID LcdSoc_MoveCursor(INT iXPos, INT iYPos)
{
    LCDSOC_CURSOR_STATE *psCursorState = &(gsLcdConfig.sCursorState);
//    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    psCursorState->iXPos = iXPos;
    psCursorState->iYPos = iYPos;
    psCursorState->bShow = (iXPos != -1);
    __LcdSoc_MoveCursor();
}

VOID LcdSoc_SetCursorRotate(INT iAngle)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    gsLcdConfig.sCursorState.iRotate = iAngle;
}

VOID LcdSoc_Reset(VOID)
{
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
	__LcdSoc_Reset();
}

VOID LcdSoc_CtrlOutput(BOOL bTurnOff)
{
    /* Wait until scan line go below VSTART */
    UINT i;
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    do 
    {
        i = ReadLcdRegisterValue(S0_VCOUNT);
    }while(i>=gsPanelInfo.ui32VStart);

	LcdBsp_CtrlOutput(bTurnOff);
}

VOID LcdSoc_GetGammaRamp(UINT16 *pui16Gamma)
{
    INT i;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (pui16Gamma)
    {
        for (i = 0; i < 256 * 3; i++)
        {
            pui16Gamma[i] = gsLcdConfig.aui8Gamma[i];
        }
    }
}

VOID LcdSoc_SetGammaRamp(UINT16 *pui16Gamma)
{
    INT                 i;

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (pui16Gamma)
    {
        for (i = 0; i < 256 * 3; i++)
        {
            gsLcdConfig.aui8Gamma[i] = (UINT8)pui16Gamma[i];
        }
        gsLcdConfig.bGammaEnable = TRUE;
        __LcdSoc_SetGammaRamp();
    }
}

VOID LcdSoc_GetColorControl(LCD_LAYER eLayer, LCD_COLORCONTROL *pData)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);
    
    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (pData->ui32Flags & LCD_COLORCONTROL_BRIGHTNESS)
    {
        pData->i32Brightness = psLayerState->i32Brightness;
    }
    if (pData->ui32Flags & LCD_COLORCONTROL_CONTRAST)
    {
        pData->i32Contrast = psLayerState->i32Contrast;
    }
    if (pData->ui32Flags & LCD_COLORCONTROL_HUE)
    {
        pData->i32Hue = psLayerState->i32Hue;
    }
    if (pData->ui32Flags & LCD_COLORCONTROL_SATURATION)
    {
        pData->i32Saturation = psLayerState->i32Saturation;
    }
}

VOID LcdSoc_SetColorControl(LCD_LAYER eLayer, LCD_COLORCONTROL *pData)
{
    LCDSOC_LAYER_STATE *psLayerState = &(gsLcdConfig.sLayerState[eLayer]);

    LCD_ENTRY(("%s\r\n",__FUNCTION__));
    if (gsLcdConfig.hVppHandle && psLayerState->bNeedVpp)
    {
        if (pData->ui32Flags & LCD_COLORCONTROL_BRIGHTNESS)
        {
            psLayerState->i32Brightness = pData->i32Brightness;
            gsVPPFuncTable.pfnUpdateBright(psLayerState->i32Brightness);
        }
        if (pData->ui32Flags & LCD_COLORCONTROL_CONTRAST)
        {
            psLayerState->i32Contrast = pData->i32Contrast;
            gsVPPFuncTable.pfnUpdateContrast(pData->i32Contrast);
        }
        if (pData->ui32Flags & LCD_COLORCONTROL_HUE)
        {
            psLayerState->i32Hue = pData->i32Hue;
			if (gsVPPFuncTable.pfnUpdateHue)
			{
            	gsVPPFuncTable.pfnUpdateHue(pData->i32Hue);
			}
        }
        if (pData->ui32Flags & LCD_COLORCONTROL_SATURATION)
        {
            psLayerState->i32Saturation = pData->i32Saturation;
			if (gsVPPFuncTable.pfnUpdateSaturation)
			{
	            gsVPPFuncTable.pfnUpdateSaturation(pData->i32Saturation);
			}
        }
    }
}

/***************************************************************************
 * Internl functions implementation
****************************************************************************/
VOID LcdSoc_PrintRegister(VOID)
{
	LCD_MSG(("LCD registers:\n"));
    LCD_MSG(("S0_HSYNC_PERIOD=0x%08x\r\n",  ReadLcdRegisterValue(S0_HSYNC_PERIOD)));
    LCD_MSG(("S0_HSYNC_WIDTH=0x%08x\r\n",   ReadLcdRegisterValue(S0_HSYNC_WIDTH)));
    LCD_MSG(("S0_VSYNC_PERIOD=0x%08x\r\n",  ReadLcdRegisterValue(S0_VSYNC_PERIOD)));
    LCD_MSG(("S0_VSYNC_WIDTH=0x%08x\r\n",   ReadLcdRegisterValue(S0_VSYNC_WIDTH)));
    LCD_MSG(("S0_ACT_HSTART=0x%08x\r\n",    ReadLcdRegisterValue(S0_ACT_HSTART)));
    LCD_MSG(("S0_ACT_VSTART=0x%08x\r\n",    ReadLcdRegisterValue(S0_ACT_VSTART)));
    LCD_MSG(("S0_ACT_HEND=0x%08x\r\n",      ReadLcdRegisterValue(S0_ACT_HEND)));
    LCD_MSG(("S0_ACT_VEND=0x%08x\r\n",      ReadLcdRegisterValue(S0_ACT_VEND)));
    LCD_MSG(("S0_OSC_RATIO=0x%08x\r\n",     ReadLcdRegisterValue(S0_OSC_RATIO)));
    LCD_MSG(("S0_TIM_CTRL=0x%08x\r\n",      ReadLcdRegisterValue(S0_TIM_CTRL)));
    LCD_MSG(("S0_TIM_STATUS=0x%08x\r\n",    ReadLcdRegisterValue(S0_TIM_STATUS)));
    LCD_MSG(("S0_HCOUNT=0x%08x\r\n",        ReadLcdRegisterValue(S0_HCOUNT)));
    LCD_MSG(("S0_VCOUNT=0x%08x\r\n",        ReadLcdRegisterValue(S0_VCOUNT)));
    LCD_MSG(("S0_BLANK=0x%08x\r\n",         ReadLcdRegisterValue(S0_BLANK)));
    LCD_MSG(("S0_BACK_COLOR=0x%08x\r\n",    ReadLcdRegisterValue(S0_BACK_COLOR)));
    LCD_MSG(("S0_DISP_MODE=0x%08x\r\n",     ReadLcdRegisterValue(S0_DISP_MODE)));
    LCD_MSG(("S0_LAYER_SEL=0x%08x\r\n",     ReadLcdRegisterValue(S0_LAYER_SEL)));
    LCD_MSG(("S0_RGB_SEQ=0x%08x\r\n",       ReadLcdRegisterValue(S0_RGB_SEQ)));
    LCD_MSG(("S0_RGB_YUV_COEF1=0x%08x\r\n", ReadLcdRegisterValue(S0_RGB_YUV_COEF1)));
    LCD_MSG(("S0_RGB_YUV_COEF2=0x%08x\r\n", ReadLcdRegisterValue(S0_RGB_YUV_COEF2)));
    LCD_MSG(("S0_RGB_YUV_COEF3=0x%08x\r\n", ReadLcdRegisterValue(S0_RGB_YUV_COEF3)));
    LCD_MSG(("S0_YUV_CTRL=0x%08x\r\n",      ReadLcdRegisterValue(S0_YUV_CTRL)));
    LCD_MSG(("S0_TV_FIELD=0x%08x\r\n",      ReadLcdRegisterValue(S0_TV_FIELD)));
    LCD_MSG(("S0_INT_LINE=0x%08x\r\n",      ReadLcdRegisterValue(S0_INT_LINE)));
    LCD_MSG(("S0_LAYER_STATUS=0x%08x\r\n",  ReadLcdRegisterValue(S0_LAYER_STATUS)));
    LCD_MSG(("DMA_STATUS=0x%08x\r\n",       ReadLcdRegisterValue(DMA_STATUS)));
    LCD_MSG(("SCR_CTRL=0X%08X\r\n",         ReadLcdRegisterValue(SCR_CTRL)));
    LCD_MSG(("INT_MASK=0X%08X\r\n",         ReadLcdRegisterValue(INT_MASK)));
    LCD_MSG(("INT_CTRL_STATUS=0X%08X\r\n",  ReadLcdRegisterValue(INT_CTRL_STATUS)));
    

    /* Lay0 register */
    LCD_MSG(("L0_CTRL=0x%08x\r\n",        ReadLcdRegisterValue(L0_CTRL)));
    LCD_MSG(("L0_HSTART=0x%08x\r\n",      ReadLcdRegisterValue(L0_HSTART)));
    LCD_MSG(("L0_VSTART=0X%08X\r\n",      ReadLcdRegisterValue(L0_VSTART)));
    LCD_MSG(("L0_HEND=0X%08X\r\n",        ReadLcdRegisterValue(L0_HEND)));
    LCD_MSG(("L0_VEND=0x%08x\r\n",        ReadLcdRegisterValue(L0_VEND)));
    LCD_MSG(("L0_BASE0=0x%08x\r\n",       ReadLcdRegisterValue(L0_BASE0)));
    LCD_MSG(("L0_BASE1=0X%08X\r\n",       ReadLcdRegisterValue(L0_BASE1)));
    LCD_MSG(("L0_XSIZE=0X%08X\r\n",       ReadLcdRegisterValue(L0_XSIZE)));
    LCD_MSG(("L0_YSIZE=0x%08x\r\n",       ReadLcdRegisterValue(L0_YSIZE)));
    LCD_MSG(("L0_SKIP=0x%08x\r\n",        ReadLcdRegisterValue(L0_SKIP)));
    LCD_MSG(("L0_DMA_CTRL=0X%08X\r\n",    ReadLcdRegisterValue(L0_DMA_CTRL)));
    LCD_MSG(("L0_ALPHA=0X%08X\r\n",       ReadLcdRegisterValue(L0_ALPHA)));
    LCD_MSG(("L0_CKEYB_SRC=0x%08x\r\n",   ReadLcdRegisterValue(L0_CKEYB_SRC)));
    LCD_MSG(("L0_CKEYS_SRC=0X%08X\r\n",   ReadLcdRegisterValue(L0_CKEYS_SRC)));
    LCD_MSG(("L0_CKEYB_DST=0x%08x\r\n",   ReadLcdRegisterValue(L0_CKEYB_DST)));
    LCD_MSG(("L0_CKEYS_DST=0X%08X\r\n",   ReadLcdRegisterValue(L0_CKEYS_DST)));
    LCD_MSG(("L0_FIFO_CHK=0X%08X\r\n",    ReadLcdRegisterValue(L0_FIFO_CHK)));
    LCD_MSG(("L0_FIFO_STATUS=0x%08x\r\n", ReadLcdRegisterValue(L0_FIFO_STATUS)));

    /* Lay1 Register */
    LCD_MSG(("L1_CTRL=0x%08x\r\n",        ReadLcdRegisterValue(L1_CTRL)));
    LCD_MSG(("L1_HSTART=0x%08x\r\n",      ReadLcdRegisterValue(L1_HSTART)));
    LCD_MSG(("L1_VSTART=0X%08X\r\n",      ReadLcdRegisterValue(L1_VSTART)));
    LCD_MSG(("L1_HEND=0X%08X\r\n",        ReadLcdRegisterValue(L1_HEND)));
    LCD_MSG(("L1_VEND=0x%08x\r\n",        ReadLcdRegisterValue(L1_VEND)));
    LCD_MSG(("L1_BASE0=0x%08x\r\n",       ReadLcdRegisterValue(L1_BASE0)));
    LCD_MSG(("L1_BASE1=0X%08X\r\n",       ReadLcdRegisterValue(L1_BASE1)));
    LCD_MSG(("L1_XSIZE=0X%08X\r\n",       ReadLcdRegisterValue(L1_XSIZE)));
    LCD_MSG(("L1_YSIZE=0x%08x\r\n",       ReadLcdRegisterValue(L1_YSIZE)));
    LCD_MSG(("L1_SKIP=0x%08x\r\n",        ReadLcdRegisterValue(L1_SKIP)));
    LCD_MSG(("L1_DMA_CTRL=0X%08X\r\n",    ReadLcdRegisterValue(L1_DMA_CTRL)));
    LCD_MSG(("L1_ALPHA=0X%08X\r\n",       ReadLcdRegisterValue(L1_ALPHA)));
    LCD_MSG(("L1_CKEYB_SRC=0x%08x\r\n",   ReadLcdRegisterValue(L1_CKEYB_SRC)));
    LCD_MSG(("L1_CKEYS_SRC=0X%08X\r\n",   ReadLcdRegisterValue(L1_CKEYS_SRC)));
    LCD_MSG(("L1_CKEYB_DST=0x%08x\r\n",   ReadLcdRegisterValue(L1_CKEYB_DST)));
    LCD_MSG(("L1_CKEYS_DST=0X%08X\r\n",   ReadLcdRegisterValue(L1_CKEYS_DST)));
    LCD_MSG(("L1_FIFO_CHK=0X%08X\r\n",    ReadLcdRegisterValue(L1_FIFO_CHK)));
    LCD_MSG(("L1_FIFO_STATUS=0x%08x\r\n", ReadLcdRegisterValue(L1_FIFO_STATUS)));

    /* Lay2 Register */
    LCD_MSG(("L2_CTRL=0x%08x\r\n",        ReadLcdRegisterValue(L2_CTRL)));
    LCD_MSG(("L2_HSTART=0x%08x\r\n",      ReadLcdRegisterValue(L2_HSTART)));
    LCD_MSG(("L2_VSTART=0X%08X\r\n",      ReadLcdRegisterValue(L2_VSTART)));
    LCD_MSG(("L2_HEND=0X%08X\r\n",        ReadLcdRegisterValue(L2_HEND)));
    LCD_MSG(("L2_VEND=0x%08x\r\n",        ReadLcdRegisterValue(L2_VEND)));
    LCD_MSG(("L2_BASE0=0x%08x\r\n",       ReadLcdRegisterValue(L2_BASE0)));
    LCD_MSG(("L2_BASE1=0X%08X\r\n",       ReadLcdRegisterValue(L2_BASE1)));
    LCD_MSG(("L2_XSIZE=0X%08X\r\n",       ReadLcdRegisterValue(L2_XSIZE)));
    LCD_MSG(("L2_YSIZE=0x%08x\r\n",       ReadLcdRegisterValue(L2_YSIZE)));
    LCD_MSG(("L2_SKIP=0x%08x\r\n",        ReadLcdRegisterValue(L2_SKIP)));
    LCD_MSG(("L2_DMA_CTRL=0X%08X\r\n",    ReadLcdRegisterValue(L2_DMA_CTRL)));
    LCD_MSG(("L2_ALPHA=0X%08X\r\n",       ReadLcdRegisterValue(L2_ALPHA)));
    LCD_MSG(("L2_CKEYB_SRC=0x%08x\r\n",   ReadLcdRegisterValue(L2_CKEYB_SRC)));
    LCD_MSG(("L2_CKEYS_SRC=0X%08X\r\n",   ReadLcdRegisterValue(L2_CKEYS_SRC)));
    LCD_MSG(("L2_CKEYB_DST=0x%08x\r\n",   ReadLcdRegisterValue(L2_CKEYB_DST)));
    LCD_MSG(("L2_CKEYS_DST=0X%08X\r\n",   ReadLcdRegisterValue(L2_CKEYS_DST)));
    LCD_MSG(("L2_FIFO_CHK=0X%08X\r\n",    ReadLcdRegisterValue(L2_FIFO_CHK)));
    LCD_MSG(("L2_FIFO_STATUS=0x%08x\r\n", ReadLcdRegisterValue(L2_FIFO_STATUS)));

    /* Lay3 Register */
    LCD_MSG(("L3_CTRL=0x%08x\r\n",        ReadLcdRegisterValue(L3_CTRL)));
    LCD_MSG(("L3_HSTART=0x%08x\r\n",      ReadLcdRegisterValue(L3_HSTART)));
    LCD_MSG(("L3_VSTART=0X%08X\r\n",      ReadLcdRegisterValue(L3_VSTART)));
    LCD_MSG(("L3_HEND=0X%08X\r\n",        ReadLcdRegisterValue(L3_HEND)));
    LCD_MSG(("L3_VEND=0x%08x\r\n",        ReadLcdRegisterValue(L3_VEND)));
    LCD_MSG(("L3_BASE0=0x%08x\r\n",       ReadLcdRegisterValue(L3_BASE0)));
    LCD_MSG(("L3_BASE1=0X%08X\r\n",       ReadLcdRegisterValue(L3_BASE1)));
    LCD_MSG(("L3_XSIZE=0X%08X\r\n",       ReadLcdRegisterValue(L3_XSIZE)));
    LCD_MSG(("L3_YSIZE=0x%08x\r\n",       ReadLcdRegisterValue(L3_YSIZE)));
    LCD_MSG(("L3_SKIP=0x%08x\r\n",        ReadLcdRegisterValue(L3_SKIP)));
    LCD_MSG(("L3_DMA_CTRL=0X%08X\r\n",    ReadLcdRegisterValue(L3_DMA_CTRL)));
    LCD_MSG(("L3_ALPHA=0X%08X\r\n",       ReadLcdRegisterValue(L3_ALPHA)));
    LCD_MSG(("L3_CKEYB_SRC=0x%08x\r\n",   ReadLcdRegisterValue(L3_CKEYB_SRC)));
    LCD_MSG(("L3_CKEYS_SRC=0X%08X\r\n",   ReadLcdRegisterValue(L3_CKEYS_SRC)));
    LCD_MSG(("L3_CKEYB_DST=0x%08x\r\n",   ReadLcdRegisterValue(L3_CKEYB_DST)));
    LCD_MSG(("L3_CKEYS_DST=0X%08X\r\n",   ReadLcdRegisterValue(L3_CKEYS_DST)));
    LCD_MSG(("L3_FIFO_CHK=0X%08X\r\n",    ReadLcdRegisterValue(L3_FIFO_CHK)));
    LCD_MSG(("L3_FIFO_STATUS=0x%08x\r\n", ReadLcdRegisterValue(L3_FIFO_STATUS)));


    if (gsLcdConfig.hVppHandle)
    {
		gsVPPFuncTable.pfnPrintRegister();
    }
}

VOID* LcdSoc_GetVppTable(VOID)
{
    if (gsLcdConfig.hVppHandle)
        return &gsVPPFuncTable;
    else
        return NULL;
}

LCD_CHIP_ID LcdSoc_GetChipID(VOID)
{
    return LCD_CHIP_V2;
}

VOID LcdSoc_SetPixelClock(UINT32 ui32PixelClock)
{
	REG_S0_OSC_RATIO reg_S0_OSC_RATIO;
	gsPanelInfo.ui32FreshRate = REFRESH_RATE(ui32PixelClock, 
		gsPanelInfo.ui32HsyncPeriod, 
		gsPanelInfo.ui32VsyncPeriod);

	reg_S0_OSC_RATIO.DW = ReadLcdRegisterValue(S0_OSC_RATIO);
	reg_S0_OSC_RATIO.DIV_RATIO = gsPanelInfo.ui32SysClock/ui32PixelClock - 1;;
	WriteLcdRegisterValue(S0_OSC_RATIO, reg_S0_OSC_RATIO.DW);	
	return;
}

UINT32 LcdSoc_GetPixelClock(VOID)
{
	return PIXEL_CLOCK(gsPanelInfo.ui32FreshRate, 
		gsPanelInfo.ui32HsyncPeriod, 
		gsPanelInfo.ui32VsyncPeriod);
}

VOID LCD_GetFuncTable(LCD_FUNCTIONTABLE *pTable)
{
    memset(pTable, 0, sizeof(LCD_FUNCTIONTABLE));

    pTable->pfnInitialize = LcdSoc_Initialize;
    pTable->pfnTerminate = LcdSoc_Terminate;
    pTable->pfnSleep = LcdSoc_Sleep;
    pTable->pfnWakeup = LcdSoc_Wakeup;
    pTable->pfnGetScanLine = LcdSoc_GetScanLine;
    pTable->pfnWaitForVBlank = LcdSoc_WaitForVBlank;
    pTable->pfnGetMode = LcdSoc_GetMode;
    pTable->pfnGetVidMem = LcdSoc_GetVidMem;

    pTable->pfnAllocOverlay = LcdSoc_AllocOverlay;
    pTable->pfnFreeOverlay = LcdSoc_FreeOverlay;

    pTable->pfnShowOverlay = LcdSoc_ShowOverlay;
    pTable->pfnSetParameters = LcdSoc_SetParameters;
    pTable->pfnGetParameters = LcdSoc_GetParameters;
    pTable->pfnHideOverlay = LcdSoc_HideOverlay;
    pTable->pfnSetOverlayPos = LcdSoc_SetOverlayPos;
	pTable->pfnPanDiaplay = LcdSoc_PanDisplay;
    pTable->pfnFlipOverlay = LcdSoc_FlipOverlay;

    pTable->pfnSetGlobalAlpha = LcdSoc_SetGlobalAlpha;
    pTable->pfnSetAlphaProperty = LcdSoc_SetAlphaProperty;
    pTable->pfnSetSrcCKey = LcdSoc_SetSrcCKey;
    pTable->pfnSetDstCKey = LcdSoc_SetDstCKey;
    pTable->pfnSetTopLayer = LcdSoc_SetTopLayer;
	pTable->pfnGetTopLayer = LcdSoc_GetTopLayer;
     
    pTable->pfnEnableInterrupt = LcdSoc_EnableInterrupt;
    pTable->pfnDisableInterrupt = LcdSoc_DisableInterrupt;
    pTable->pfnClearInterrupt = LcdSoc_ClearInterrupt;
    pTable->pfnIsInterrupted = LcdSoc_IsInterrupted;
    
    pTable->pfnSetCursorShape = LcdSoc_SetCursorShape;
    pTable->pfnMoveCursor = LcdSoc_MoveCursor;
    pTable->pfnSetCursorRotate = LcdSoc_SetCursorRotate;

    pTable->pfnGetGammaRamp = LcdSoc_GetGammaRamp;
    pTable->pfnSetGammaRamp = LcdSoc_SetGammaRamp;
    pTable->pfnGetColorControl = LcdSoc_GetColorControl;
    pTable->pfnSetColorControl = LcdSoc_SetColorControl;

    pTable->pfnGetVppTable = LcdSoc_GetVppTable;

    pTable->pfnPrintRegister = LcdSoc_PrintRegister;
    pTable->pfnReset = LcdSoc_Reset;
	pTable->pfnCtrlOutput = LcdSoc_CtrlOutput;
    pTable->pfnGetChipID = LcdSoc_GetChipID;
	pTable->pfnSetPixelClock = LcdSoc_SetPixelClock;
	pTable->pfnGetPixelClock = LcdSoc_GetPixelClock;

	pTable->pfnChangeMode = LcdSoc_ChangeMode;
}


