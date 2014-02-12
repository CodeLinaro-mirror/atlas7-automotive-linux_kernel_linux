/*
 * CSR SiRFprima2 VIP library definitions
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include "CspSocVipInternal.h"

#if defined(_WIN32_WCE)

#include "macros.h"
#include <drvlib.h>

/*
** Global variable
*/
VIPSOC_CONFIG gsVipConfig = {0};

INLINE VOID OSVipWaitms(UINT uCount)
{
    msWait(uCount);
}

INLINE VOID OSVipWaitus(UINT uCount)
{
    usWait(uCount);
}


VOID __VipSoc_EnableClock(VOID)
{
    /* Enable camera power clock */
    WRITE_BITFIELD(struct clkclkenable1, &(v_pClkRegs->clk_clkenable_1), cam, 1);
    /* Enable DMA power clock */
    WRITE_BITFIELD(struct clkclkenable1, &(v_pClkRegs->clk_clkenable_1), dma1, 1);
}

VOID __VipSoc_DisableClock(VOID)
{
    /* Disable camera power clock */
    WRITE_BITFIELD(struct clkclkenable1, &(v_pClkRegs->clk_clkenable_1), cam, 0);
}

VOID __VipSoc_SetPinMux()
{
    /* enable pin multiplex to vip port, PAD_VIPROM_EN:bit0 */
    WRITE_BITFIELD(struct RSCPinMuxBits, &(v_pRscRegs->rscPinMux),  rom, 0);

    /*set GPIO pins to camera interface; Move to Camera and DVD driver      */
    IOW_REG_AND(UONG, &(v_pGpioRegs->gpio[2].paden), (0xfc007fff));
}

#if !NOSYSCALL
VOID *OSMapVipRegs(VOID)
{
    if (CspRegMap(FALSE))
    {
        return (VOID*)v_pCameraReg;
    }
    else
    {
        VIP_MSG((VIP_STR("OSMapVipRegs: CspRegMap fail\n")));
        return NULL;
    }
}

VOID OSUnmapVipRegs(VOID)
{
    CspRegUnMap();
}

VOID *OSMapDMARegs(VOID)
{
    if (CspRegMap(FALSE))
    {
        return (VOID*)v_pDMA1Reg;
    }
    else
    {
        VIP_MSG((VIP_STR("OSMapDMARegs: CspRegMap fail\n")));
        return NULL;
    }
}

void OSUnmapDMARegs(void)
{
    CspRegUnMap();
}

#else
VOID *OSMapVipRegs(VOID)
{
    return (VOID*)v_pCameraReg;
}

VOID OSUnmapVipRegs(VOID)
{

}

VOID *OSMapDMARegs(VOID)
{
    return (VOID*)v_pDMA1Reg;
}

void OSUnmapDMARegs(void)
{
}
#endif

#else
#include <linux/kernel.h>
#include <linux/string.h>
/*
** Global variable
*/
VIPSOC_CONFIG gsVipConfig = {
    .bInitialized = FALSE,
};

VOID *OSMapVipRegs(VOID)
{
    return NULL;
}

VOID OSUnmapVipRegs(VOID)
{

}

VOID *OSMapDMARegs(VOID)
{
    return NULL;

}
void OSUnmapDMARegs(void)
{

}

INLINE VOID OSVipWaitms(UINT uCount)
{

}

INLINE VOID OSVipWaitus(UINT uCount)
{
}
VOID __VipSoc_EnableClock(VOID)
{
}

VOID __VipSoc_DisableClock(VOID)
{

}

VOID __VipSoc_SetPinMux(VOID)
{

}

#endif

static VOID __VipSoc_Reset(VOID)
{
    REG_CAM_CTRL        RegCamCtrl;

    RegCamCtrl.INIT                 = 1;
    /* reset camera */
    WriteVipRegisterValue(CAM_CTRL, RegCamCtrl.DW);
    OSVipWaitms(50);
    RegCamCtrl.INIT                 = 0;
    WriteVipRegisterValue(CAM_CTRL, RegCamCtrl.DW);
}


static VOID __VipSoc_Setup(VOID)
{
    REG_CAM_START                   RegCamStart;
    REG_CAM_END                     RegCamEnd;
    REG_CAM_CTRL                    RegCamCtrl;
    REG_CAM_PIXEL_SHIFT             RegCamPixelShift;
    REG_CAM_FIFO_OP                 RegCamFifoOp;
    REG_CAM_FIFO_LEVEL_CHK          RegCamFifoLevelChk;
    REG_CAM_DMA_CTRL                RegCamDmaCtrl;

    __VipSoc_SetPinMux();

    /* Reset camera                                                         */
    __VipSoc_Reset();

    /* Disable camera YCOUNT interrupt                                      */
    WriteVipRegisterValue(CAM_INT_COUNT, 0x7fff7fff);
    /* Disable all camera interrupt at start                                */
    WriteVipRegisterValue(CAM_INT_EN, 0);

    /* Set active window with maximum values                                */
    RegCamStart.DW                  = 0;
    RegCamEnd.DW                    = 0;
    RegCamStart.XS                  = 0x4fff;
    RegCamStart.YS                  = 0x4fff;
    RegCamEnd.XE                    = 0xffff;
    RegCamEnd.YE                    = 0xffff;

    WriteVipRegisterValue(CAM_START, RegCamStart.DW);
    WriteVipRegisterValue(CAM_END,   RegCamEnd.DW);

    RegCamCtrl.DW                   = 0;
    /*  set capture mode to contineous by default                           */
    RegCamCtrl.SINGLE               = 0;
    /*default set to 0 (vip don't do yuv2rgb, the job would be done by vpp) */
    RegCamCtrl.YUVRGB               = 0;
    RegCamCtrl.X_SCA                = 0;
    RegCamCtrl.Y_SCA                = 0;

    WriteVipRegisterValue(CAM_CTRL, RegCamCtrl.DW);

    /* Default select pxd_data[15:0] as valid data                          */
    RegCamPixelShift.DW = 0;
    RegCamPixelShift.PIXEL_SHIFT    = VIP_PIXELSET_DATAPIN_0TO7;

    WriteVipRegisterValue(CAM_PIXEL_SHIFT, RegCamPixelShift.DW);

    /* Disable FIFO                                                         */
    RegCamFifoOp.DW                 = 0;
    RegCamFifoOp.FIFO_START         = 0;

    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);

    /* Set FIFO config data, high check, low check and stop check.          */
    RegCamFifoLevelChk.DW           = 0;
    RegCamFifoLevelChk.FIFO_SC      = 0x4;
    RegCamFifoLevelChk.FIFO_LC      = 0x8;
    RegCamFifoLevelChk.FIFO_HC      = 0x10;
    WriteVipRegisterValue(CAM_FIFO_LEVEL_CHECK, RegCamFifoLevelChk.DW);

    RegCamDmaCtrl.DW                = 0;
    RegCamDmaCtrl.DMA_FLUSH         = 1;
    /* exchange word order in DWORD of the input data from TV-decoder, for SAA7111A only??? */
    RegCamDmaCtrl.DW                &= ~VIP_DMA_ENDIAN_MASK;
    RegCamDmaCtrl.DW                |= VIP_DMA_ENDIAN_WXDW;
    WriteVipRegisterValue(CAM_DMA_CTRL, RegCamDmaCtrl.DW);

    if (gsVipConfig.pDMARegs != NULL)
    {
        REG_DMA_CHN_VALID               RegDmaChnValid;
        REG_DMA_INT_ENABLE              RegDmaIntEnable;

        RegDmaChnValid.DW               = ReadDmaRegisterValue(DMA_CHN_VALID);
        RegDmaIntEnable.DW              = ReadDmaRegisterValue(DMA_CHN_INT_ENABLE);

        /*Disable DMA1 channel 0 at open                                        */
        RegDmaChnValid.chn0             = 0;
        /*Disable DMA channel 0 interrupt at open                               */
        RegDmaIntEnable.chn0            = 0;

        WriteDmaRegisterValue(DMA_CHN_VALID, RegDmaChnValid.DW);
        WriteDmaRegisterValue(DMA_CHN_INT_ENABLE, RegDmaIntEnable.DW);
    }
}

static VOID __VipSoc_SetSize(VOID)
{
    REG_CAM_START       RegCamStart;
    REG_CAM_END         RegCamEnd;

    /*----------------------------- Set active window ----------------------------- */
    RegCamStart.DW                  = 0;
    RegCamEnd.DW                    = 0;
    RegCamStart.XS                  = gsVipConfig.VipSetting.SrcRect.left;
    RegCamStart.YS                  = gsVipConfig.VipSetting.SrcRect.top;
    RegCamEnd.XE                    = gsVipConfig.VipSetting.SrcRect.right;
    RegCamEnd.YE                    = gsVipConfig.VipSetting.SrcRect.bottom;

    WriteVipRegisterValue(CAM_START, RegCamStart.DW);
    WriteVipRegisterValue(CAM_END,   RegCamEnd.DW);
}

static VOID __VipSoc_SetParms(VOID)
{
    REG_CAM_CTRL                    RegCamCtrl;
    REG_CAM_PIXEL_SHIFT             RegCamPixelShift;
    UINT32 RawPixelFormat           = 0;
    BOOL   bEnableYUV2RGB           = 0;
    UINT32 OutputFormat             = 0;
    UINT32 dwInputPxlStride         = 0;

    RegCamCtrl.DW                   = ReadVipRegisterValue(CAM_CTRL);
    RegCamPixelShift.DW             = ReadVipRegisterValue(CAM_PIXEL_SHIFT);

    switch(gsVipConfig.VipSetting.eSrcFormat)
    {
        case LCD_PIXELFORMAT_YUYV:
            RawPixelFormat = VIP_CTRL_YUVSEQ_YUYV;
            dwInputPxlStride = 2;
            break;
        case LCD_PIXELFORMAT_UYYV:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_UYYV;
            break;
        case LCD_PIXELFORMAT_YUVY:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_YUVY;
            break;
        case LCD_PIXELFORMAT_UYVY:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_UYVY;
            break;
        case LCD_PIXELFORMAT_YVYU:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_YVYU;
            break;
        case LCD_PIXELFORMAT_VYYU:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_VYYU;
            break;
        case LCD_PIXELFORMAT_YVUY:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_YVUY;
            break;

        case LCD_PIXELFORMAT_VYUY:
            dwInputPxlStride = 2;
            RawPixelFormat = VIP_CTRL_YUVSEQ_VYUY;
            break;
        default:
            VIP_ASSERT(0);
            VIP_MSG((VIP_STR("%s(%d): unknown input format 0x%0x\r\n"), __FUNCTION__, __LINE__, RawPixelFormat));
            break;
    }

    switch(gsVipConfig.VipSetting.eDstFormat)
    {
        case LCD_PIXELFORMAT_UNKNOWN:
            bEnableYUV2RGB = FALSE;
            break;

        case LCD_PIXELFORMAT_565:
            bEnableYUV2RGB = TRUE;
            OutputFormat   = 0x3;
            break;
        case LCD_PIXELFORMAT_8880:
            bEnableYUV2RGB = TRUE;
            OutputFormat   = 0x00;
            break;

        default:
            VIP_ASSERT(0);
            VIP_MSG((VIP_STR("%s(%d): unknown output format 0x%0x\r\n"), __FUNCTION__, __LINE__, gsVipConfig.VipSetting.eDstFormat));
            break;
    }

    RegCamCtrl.YUV_FORMAT           = RawPixelFormat;

    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_PXCLK_CTRL)
    {
        RegCamCtrl.PXCLK_CTRL = 1;
    }
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_HSYNC_CTRL)
    {
        RegCamCtrl.HSYNC_CTRL = 1;
    }
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_VSYNC_CTRL)
    {
        RegCamCtrl.VSYNC_CTRL = 1;
    }
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_PIXCLK_INV)
    {
        RegCamCtrl.PIXCLK_INV = 1;
    }
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_HSYNC_INV)
    {
        RegCamCtrl.HSYNC_INV = 1;
    }
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_VSYNC_INV)
    {
        RegCamCtrl.VSYNC_INV = 1;
    }
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_CCIR656_EN)
    {
        RegCamCtrl.CCIR656_EN = 1;
    }
	if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_SINGLE_MODE) {
		RegCamCtrl.SINGLE = 1;
	} else {
		RegCamCtrl.SINGLE = 0;
	}
#ifdef CONFIG_ARCH_ATLAS6
    if (gsVipConfig.VipSetting.uiFlag & VIP_CTRL_PAD_MUX_UPLI)
    {
        RegCamCtrl.PAD_MUX_ON_UPLI = 1;
    }
#endif

    if (bEnableYUV2RGB)
    {
        VIP_MSG((VIP_STR("YUV to RGB enabled!\r\n")));

        WriteVipRegisterValue(CAM_YUV_COEFR, 0x12A00198);
        WriteVipRegisterValue(CAM_YUV_COEFG, 0x12A190D0);
        WriteVipRegisterValue(CAM_YUV_COEFB, 0x12A81000);
        WriteVipRegisterValue(CAM_YUV_OFFSET,0x115220DF);
        RegCamCtrl.YUVRGB           = 1;
        RegCamCtrl.OUT_FORMAT       = OutputFormat;
    }
    else
    {
        VIP_MSG((VIP_STR("YUV to RGB disabled!\r\n")));
        RegCamCtrl.YUVRGB           = 0;
    }

    WriteVipRegisterValue(CAM_CTRL, RegCamCtrl.DW);

    RegCamPixelShift.PIXEL_SHIFT    = gsVipConfig.VipSetting.PixelBitSelect;
    WriteVipRegisterValue(CAM_PIXEL_SHIFT, RegCamPixelShift.DW);

    /* first column, first row */
    WriteVipRegisterValue(CAM_INT_COUNT, 0x00100001);
    WriteVipRegisterValue(CAM_DMA_LEN,  0);

    __VipSoc_SetSize();

}

static VOID __VipSoc_Start(VOID)
{
    REG_CAM_FIFO_OP                 RegCamFifoOp;

    /*********************** Set Vip Register *********************************/
    /* Reset FIFO                                                         */
    RegCamFifoOp.DW             = 0;

    RegCamFifoOp.FIFO_RESET     = 1;
    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);

    RegCamFifoOp.FIFO_RESET     = 0;
    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);

    /*clear all interrupts                                                */
    WriteVipRegisterValue(CAM_INT_CTRL, VIP_INTMASK_ALL);

    /*Open Overflow and underflow interrupt                               */
    WriteVipRegisterValue(CAM_INT_EN, VIP_INTMASK_SENSOR | VIP_INTMASK_FIFO_UFLOW | VIP_INTMASK_FIFO_OFLOW);

    /* Start FIFO transfer to DMA */
    RegCamFifoOp.FIFO_START     = 1;
    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);
}

static VOID __VipSoc_Stop(VOID)
{
    REG_CAM_FIFO_OP                 RegCamFifoOp;

    RegCamFifoOp.DW                  = 0;
    /* Stop the FIFO first */
    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);

    /* Disable camera interupt */
    WriteVipRegisterValue(CAM_INT_EN, 0);
}

static VOID __VipSoc_ResetFIFO(VOID)
{
    REG_CAM_FIFO_OP                 RegCamFifoOp;

    RegCamFifoOp.DW             = ReadVipRegisterValue(CAM_FIFO_OP_REG);

    RegCamFifoOp.FIFO_RESET     = 1;
    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);

    RegCamFifoOp.FIFO_RESET     = 0;
    WriteVipRegisterValue(CAM_FIFO_OP_REG, RegCamFifoOp.DW);
}

static UINT32 VipSoc_GetInterrupts(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"), __FUNCTION__));

    return (ReadVipRegisterValue(CAM_INT_EN) &
        ReadVipRegisterValue(CAM_INT_CTRL)) &
        VIP_INTMASK_ALL;
}

static VOID VipSoc_ClearInterrupts(UINT32 status)
{
    VIP_ENTRY((VIP_STR("%s\r\n"), __FUNCTION__));

    WriteVipRegisterValue(CAM_INT_CTRL, status & VIP_INTMASK_ALL);
}

static UINT32 VipSoc_GetFID(VOID)
{
    REG_CAM_CTRL    RegCamCtrl;
    RegCamCtrl.DW   = ReadVipRegisterValue(CAM_CTRL);

    return RegCamCtrl.FID;
}

static BOOL __VipSoc_IsBusy(VOID)
{
    UINT32 dwValue                  = 0;

    dwValue                         = ReadVipRegisterValue(CAM_FIFO_OP_REG);

    if(dwValue & 0x1)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static VOID VipSoc_Initialize(VOID *pVipRegs, VOID *pDMARegs)
{
    VIP_ENTRY((VIP_STR("%s\r\n"), __FUNCTION__));

    if (!gsVipConfig.bInitialized)
    {
        memset(&gsVipConfig, 0, sizeof(gsVipConfig));

        gsVipConfig.bInitialized = TRUE;

        gsVipConfig.pVipRegs = pVipRegs;
        gsVipConfig.pDMARegs = pDMARegs;

        __VipSoc_EnableClock();
    }

    gsVipConfig.ui32RefCount++;
    return;
}

static VOID VipSoc_Terminate(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    gsVipConfig.ui32RefCount--;


    if (gsVipConfig.ui32RefCount == 0)
    {
        gsVipConfig.bInitialized = FALSE;

        __VipSoc_DisableClock();
        gsVipConfig.pVipRegs = NULL;
        gsVipConfig.pDMARegs = NULL;
    }
}

static BOOL VipSoc_SetParams(IN VIP_PARAMS* pData)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    gsVipConfig.VipSetting = *pData;

	 __VipSoc_Setup();

    __VipSoc_SetParms();

    return TRUE;
}

static VOID VipSoc_SetBase(IN UINT32 PhyAddr)
{
   VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

   gsVipConfig.VipSetting.ui32DstBase = PhyAddr;

   return;
}

static BOOL VipSoc_Start(BOOL bLoopMode)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    gsVipConfig.VipSetting.bLoopMode = bLoopMode;


    __VipSoc_Start();

    return TRUE;
}

static BOOL VipSoc_Stop(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    __VipSoc_Stop();

    return TRUE;
}


static VOID VipSoc_Sleep(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    __VipSoc_DisableClock();

    return;
}

static VOID VipSoc_Wakeup(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    __VipSoc_EnableClock();
    __VipSoc_Setup();

    __VipSoc_SetParms();

    return;
}

static VOID VipSoc_ResetFIFO(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    __VipSoc_ResetFIFO();
}
static BOOL VipSoc_Reset(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    __VipSoc_Reset();

    return TRUE;
}

static BOOL VipSoc_IsBusy(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    return __VipSoc_IsBusy();
}


static VOID VipSoc_SaveVipConfig(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    memcpy(&gsVipConfig.VipSavedSetting,&gsVipConfig.VipSetting,sizeof(VIP_PARAMS));
}

static VOID VipSoc_RestoreVipConfig(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    memcpy(&gsVipConfig.VipSetting,&gsVipConfig.VipSavedSetting,sizeof(VIP_PARAMS));
    __VipSoc_SetParms();
}

static VOID VipSoc_PrintRegister(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    VIP_MSG((VIP_STR("VIP register\r\n")));

    VIP_MSG((VIP_STR("CAM_COUNT             = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_COUNT)));
    VIP_MSG((VIP_STR("CAM_INT_COUNT         = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_INT_COUNT)));
    VIP_MSG((VIP_STR("CAM_START             = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_START)));
    VIP_MSG((VIP_STR("CAM_END               = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_END)));
    VIP_MSG((VIP_STR("CAM_CTRL              = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_CTRL)));
    VIP_MSG((VIP_STR("CAM_PIXEL_SHIFT       = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_PIXEL_SHIFT)));
    VIP_MSG((VIP_STR("CAM_YUV_COEFR         = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_YUV_COEFR)));
    VIP_MSG((VIP_STR("CAM_YUV_COEFG         = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_YUV_COEFG)));
    VIP_MSG((VIP_STR("CAM_YUV_COEFB         = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_YUV_COEFB)));
    VIP_MSG((VIP_STR("CAM_YUV_OFFSET        = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_YUV_OFFSET)));
    VIP_MSG((VIP_STR("CAM_INT_EN            = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_INT_EN)));
    VIP_MSG((VIP_STR("CAM_INT_CTRL          = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_INT_CTRL)));
    VIP_MSG((VIP_STR("CAM_VSYNC_CTRL        = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_VSYNC_CTRL)));
    VIP_MSG((VIP_STR("CAM_HSYNC_CTRL        = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_HSYNC_CTRL)));
    VIP_MSG((VIP_STR("CAM_PXCLK_CTRL        = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_PXCLK_CTRL)));
    VIP_MSG((VIP_STR("CAM_VSYNC_HSYNC       = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_VSYNC_HSYNC)));
    VIP_MSG((VIP_STR("CAM_TIMING_CTRL       = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_TIMING_CTRL)));
    VIP_MSG((VIP_STR("CAM_DMA_CTRL          = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_DMA_CTRL)));
    VIP_MSG((VIP_STR("CAM_DMA_LEN           = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_DMA_LEN)));
    VIP_MSG((VIP_STR("CAM_FIFO_CTRL_REG     = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_FIFO_CTRL_REG)));
    VIP_MSG((VIP_STR("CAM_FIFO_LEVEL_CHECK  = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_FIFO_LEVEL_CHECK)));
    VIP_MSG((VIP_STR("CAM_FIFO_OP_REG       = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_FIFO_OP_REG)));
    VIP_MSG((VIP_STR("CAM_FIFO_STATUS_REG   = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_FIFO_STATUS_REG)));
    VIP_MSG((VIP_STR("CAM_RD_FIFO_DATA      = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_RD_FIFO_DATA)));
    VIP_MSG((VIP_STR("CAM_TS_CTRL           = 0x%.8x\r\n"),  ReadVipRegisterValue(CAM_TS_CTRL)));

}

static VOID VipSoc_Lock(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));
}

static VOID VipSoc_Unlock(VOID)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));
}

static VOID VipSoc_Reserved(VOID)
{
     VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));
}

VOID VIP_GetFuncTable(VIP_FUNCTIONTABLE *pTable)
{
    VIP_ENTRY((VIP_STR("%s\r\n"),__FUNCTION__));

    memset(pTable, 0, sizeof(VIP_FUNCTIONTABLE));

    pTable->pfnInitialize       = VipSoc_Initialize;
    pTable->pfnTerminate        = VipSoc_Terminate;

    pTable->pfnSetParams        = VipSoc_SetParams;

    pTable->pfnSetBase          = VipSoc_SetBase;

    pTable->pfnStart            = VipSoc_Start;
    pTable->pfnStop             = VipSoc_Stop;

    pTable->pfnSleep            = VipSoc_Sleep;
    pTable->pfnWakeup           = VipSoc_Wakeup;

    pTable->pfnReset            = VipSoc_Reset;
    pTable->pfnIsBusy           = VipSoc_IsBusy;
    pTable->pfnSaveConfig       = VipSoc_SaveVipConfig;
    pTable->pfnRestoreConfig    = VipSoc_RestoreVipConfig;
    pTable->pfnGetInterrupts	= VipSoc_GetInterrupts;
    pTable->pfnClearInterrupts	= VipSoc_ClearInterrupts;

    pTable->pfnResetFIFO        = VipSoc_ResetFIFO;

    pTable->pfnGetFID        = VipSoc_GetFID;

    /* debug only */
    pTable->pfnPrintRegister    = VipSoc_PrintRegister;

    /* Option */
    pTable->pfnLock             = VipSoc_Lock;
    pTable->pfnUnlock           = VipSoc_Unlock;
    pTable->pfnReserved         = VipSoc_Reserved;
}
