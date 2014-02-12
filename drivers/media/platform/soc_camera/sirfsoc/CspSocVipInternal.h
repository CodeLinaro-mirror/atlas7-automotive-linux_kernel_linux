/*
 * CSR SiRFprima2 VIP library internal definitions
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __CSP_SOC_VIP_INTERNAL_H__
#define __CSP_SOC_VIP_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "CspCmnVip.h"
#include "VIPRegs.h"

typedef struct _VIPSOC_CONFIG_
{
    BOOL bInitialized;
    UINT32 ui32RefCount;
    VIP_PARAMS VipSetting;

    volatile unsigned char *pVipRegs;
    volatile unsigned char *pDMARegs;
    VIP_PARAMS VipSavedSetting;
} VIPSOC_CONFIG;

extern VIPSOC_CONFIG gsVipConfig;

/*
** Register operation
*/
static INLINE UINT32 ReadVipRegisterValue(UINT32 ui32Offset)
{
	return (*(volatile UINT32 * const)(gsVipConfig.pVipRegs + ui32Offset));
}

static INLINE UINT32 ReadDmaRegisterValue(UINT32 ui32Offset)
{
    return (*(volatile UINT32 * const)(gsVipConfig.pDMARegs + ui32Offset));
}

static INLINE VOID __WriteVipRegisterValue(UINT32 ui32Offset, UINT32 ui32Value)
{
	*(volatile UINT32 * const)(gsVipConfig.pVipRegs + ui32Offset) = ui32Value;
}

static INLINE VOID WriteDmaRegisterValue(UINT32 ui32Offset, UINT32 ui32Value)
{
	*(volatile UINT32 * const)(gsVipConfig.pDMARegs + ui32Offset) = ui32Value;
}

#ifdef VIP_LOG
#define WriteVipRegisterValue(ui32Offset, ui32Value) \
	do \
	{ \
		VIP_MSG((LCD_STR("WriteVipRegisterValue(") RAW_STR(#ui32Offset) RAW_STR(", 0x%08x);\r\n"), ui32Value)); \
		__WriteVipRegisterValue(ui32Offset, ui32Value); \
	} while(0);
#else
#define WriteVipRegisterValue(ui32Offset, ui32Value) __WriteVipRegisterValue(ui32Offset, ui32Value)
#endif

#if defined(__cplusplus)
}
#endif

#endif

