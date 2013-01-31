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
#ifndef __CSP_SOC_VPP_INTERNAL_H__
#define __CSP_SOC_VPP_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "CspCmnLcd.h"
#include "CspCmnVpp.h"
#include "VPPV2Regs.h"

typedef struct _VPPSOC_CONFIG_
{
    VPP_SETPARAMS_DATA sSurfaceState;
    RECT sRectSrc;
    RECT sRectDst;

    BOOL bContinueLock;
    BOOL bShow;
    volatile unsigned char *pVppRegs;
    BOOL bNeedUnmap;

    UINT ui32RefCount;
    BOOL bInitialized;

    BOOL bValid;
	BOOL bUserMode;

    BOOL bUVInterleave;

    BOOL bDMAInterruptEnabled;

    VPP_COLORCTRL_DATA sClrCtrl;
    VPP_INTERLACE_DATA sInterlace;

	INT16 i16Hue;
	INT16 i16Saturation;

    DOUBLE fHScalingRatioLast;
    DOUBLE fVScalingRatioLast;
} VPPSOC_CONFIG;

extern VPPSOC_CONFIG gsVppConfig;

#if 1
typedef enum _VPP_INFORMAT_
{
    VPP_INFORMAT_UNKNOWN = 0,

    /*
      RGB format goes here
    */
    VPP_INFORMAT_YUV420,
    VPP_INFORMAT_Y0UY1V,
    VPP_INFORMAT_Y1UY0V,
    VPP_INFORMAT_Y0VY1U,
    VPP_INFORMAT_Y1VY0U,
    VPP_INFORMAT_UY0VY1,
    VPP_INFORMAT_UY1VY0,
    VPP_INFORMAT_VY0UY1,
    VPP_INFORMAT_VY1UY0
    
}VPP_INFORMAT;

typedef enum _VPP_OUTFORMAT_
{
    VPP_OUTFORMAT_UNKNOWN = 0,

    /*
      RGB format goes here
    */
    VPP_OUTFORMAT_RGB565,
    VPP_OUTFORMAT_RGB666,
    VPP_OUTFORMAT_RGB888,
    VPP_OUTFORMAT_Y0UY1V,
    VPP_OUTFORMAT_Y1UY0V,
    VPP_OUTFORMAT_Y0VY1U,
    VPP_OUTFORMAT_Y1VY0U,
    VPP_OUTFORMAT_UY0VY1,
    VPP_OUTFORMAT_UY1VY0,
    VPP_OUTFORMAT_VY0UY1,
    VPP_OUTFORMAT_VY1UY0
    
}VPP_OUTFORMAT;

#endif

/*
** Register operation
*/
static INLINE UINT32 ReadVppRegisterValue(UINT32 ui32Offset)
{
	return (*(volatile UINT32 * const)(gsVppConfig.pVppRegs + ui32Offset));
}

static INLINE VOID WriteVppRegisterValue(UINT32 ui32Offset, UINT32 ui32Value)
{
#ifdef VPP_LOG
	LCD_MSG(("Write VPP 0x%08x=0x%08x \r\n",  ui32Offset, ui32Value));
#endif
	*(volatile UINT32 * const)(gsVppConfig.pVppRegs + ui32Offset) = ui32Value;
}

static INLINE UINT ALIGN_SIZE(UINT uiSize, UINT uiAlign)
{
	return (uiSize+uiAlign-1) & ~(uiAlign-1);
}

#if defined(__cplusplus)
}
#endif

#endif

