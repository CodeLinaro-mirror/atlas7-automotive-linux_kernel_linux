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
@Date           8 December 2009

@Platform       Generic

@Description    CSP COM Interface for VPP
****************************************************************************/

#ifndef CSP_CMN_VPP_H
#define CSP_CMN_VPP_H

#if defined(__cplusplus)
extern "C" {
#endif

/* Don't forget to adjust function convertDi2HWDi if it's changed*/
typedef enum _VPP_DI_MODE_
{
    VPP_DI_RESERVED = 0,
    VPP_DI_WEAVE = 1,
    VPP_DI_3MEDIAN = 2,
    /* Vertical Median Ranking Interpolation */
    VPP_DI_VMRI = 3,    
} VPP_DI_MODE;

typedef enum _VPP_OUTPUT_MODE_
{
    VPP_OUTPUT_P_SINGLE = 0,
    VPP_OUTPUT_INTERLACE = 1,
    VPP_OUTPUT_P_DOUBLE = 2,
} VPP_OUTPUT_MODE;


typedef struct _VPP_INTERLACE_DATA_
{
    BOOL bInInterlaced;
    VPP_OUTPUT_MODE eOutMode;
    BOOL bOutputTopFirst;
    BOOL bInputTopFirst;
    BOOL bTopDi;
    VPP_DI_MODE eDeintMode;
    UINT32 ui32FieldOffset;
}VPP_INTERLACE_DATA;

typedef struct _VPP_COLORCTRL_DATA_
{
    INT16 i16Bright;
    INT16 i16Contrast;
    INT16 i16UC;
    INT16 i16VC;
}VPP_COLORCTRL_DATA;

typedef struct _VPP_SETPARAMS_DATA_
{
    UINT32 ui32SrcBase;            /* IN: Physical address of surface */
    LCD_PIXELFORMAT eSrcFormat;    /* IN: Format of surface. Refer to FOURCC format of LCD_PIXELFORMAT*/
    UINT uiSrcWStride_pixel;       /* IN: Width stride in pixel unit */
    UINT uiSrcHStride_pixel;       /* IN: Heigth stride in pixel unit */
    LCD_PIXELFORMAT eDstFormat;    /* IN: Output format*/
    UINT32 ui32DstBase;            /* IN: if =0, pass through mode */
    UINT uiDstWStride_pixel;       /* IN: Width stride in pixel unit */
    UINT uiDstHStride_pixel;       /* IN: Heigth stride in pixel unit */    
}VPP_SETPARAMS_DATA;

typedef struct _VPP_YUV2RGB_DATA_
{
    UINT32 ui32YCoeff;
    UINT32 ui32UCoeff;
    UINT32 ui32VCoeff;
    UINT32 ui32Offset;
}VPP_YUV2RGB_DATA;

typedef VOID (*PFN_VPP_INITIALIZE)(VOID *pVppRegs);
typedef VOID (*PFN_VPP_TERMINATE)(VOID);
typedef BOOL (*PFN_VPP_LOCK)(BOOL bContinues);
typedef VOID (*PFN_VPP_UNLOCK)(VOID);
typedef BOOL (*PFN_VPP_ALLOCOVERLAY)(LCD_ALLOCOVERLAY_DATA *pData);

/* Set parameters which will be set only once */
typedef BOOL (*PFN_VPP_SETPARAMES)(VPP_SETPARAMS_DATA *pData);
typedef VOID (*PFN_VPP_SETBASE)(UINT32 ui32Base);
typedef BOOL (*PFN_VPP_SETSIZE)(RECT *psSrcRect, RECT *psDstRect);	 
typedef VOID (*PFN_VPP_START)(BOOL bContinues);
typedef VOID (*PFN_VPP_STOP)(VOID);
typedef BOOL (*PFN_VPP_ISBUSY)(VOID);
typedef VOID (*PFN_VPP_SETINTERLACE)(BOOL bInputMode, VPP_OUTPUT_MODE eOutputMode, 
		BOOL bOutputTopFirst, BOOL bTopFieldReserved, VPP_DI_MODE eDeinterMode,
		BOOL bInputTopFirst, UINT32 ui32FieldOffset);
typedef VOID (*PFN_VPP_CLEARDMAINTERRUPT)(VOID);
typedef VOID (*PFN_VPP_ENABLEDMAINTERRUPT)(VOID);
typedef VOID (*PFN_VPP_DISABLEDMAINTERRUPT)(VOID);
typedef BOOL (*PFN_VPP_ISDMAINTERRUPTED)(VOID);
typedef VOID (*PFN_VPP_UPDATEFORMATENDIAN)(BOOL bInputBigEndian, BOOL bOutputBigEndian);

typedef VOID (*PFN_VPP_SLEEP)(VOID);
typedef VOID (*PFN_VPP_WAKEUP)(VOID);
typedef VOID (*PFN_VPP_SETCOLORCTRL)(VPP_COLORCTRL_DATA *pData);
typedef VOID (*PFN_VPP_UPDATEHUE)(INT iHue);
typedef VOID (*PFN_VPP_UPDATESATURATION)(INT iSaturation);
typedef VOID (*PFN_VPP_UPDATEBRIGHT)(INT iBrightness);
typedef VOID (*PFN_VPP_UPDATECONTRAST)(INT iContrast);
typedef VOID (*PFN_VPP_UPDATECOEFF)(VOID);
typedef VOID (*PFN_VPP_UPDATECOEFF2)(UINT32 *pFilterCoef);
typedef VOID (*PFN_VPP_UPDATEYUV2RGB)(VPP_YUV2RGB_DATA *pRCoef, VPP_YUV2RGB_DATA *pGCoef, VPP_YUV2RGB_DATA *pBCoef);
typedef VOID (*PFN_VPP_PRINTREGISTER)(VOID);

/*Internal debug function*/
typedef VOID (*PFN_VPP_GETSOURCEINFO)(UINT *pwidth,UINT *pheight,INT *pformat);
typedef BOOL (*PFN_VPP_GETSOURCEBUFFER)(UINT8 *pSrc);
typedef VOID (*PFN_VPP_GETDESTINFO)(UINT *pwidth,UINT *pheight,INT *pformat);
typedef BOOL (*PFN_VPP_GETDESTBUFFER)(UINT8* pDest);
typedef VOID (*PFN_VPP_SETUSERMODE)(BOOL bUser);
typedef VOID (*PFN_VPP_RESET)(VOID);


typedef struct _VPP_FUNCTIONTABLE_
{
    PFN_VPP_INITIALIZE pfnInitialize;
    PFN_VPP_TERMINATE pfnTerminate;
    PFN_VPP_LOCK pfnLock;
    PFN_VPP_UNLOCK pfnUnlock;
	PFN_VPP_ALLOCOVERLAY pfnAllocOverlay;

    /* Set parameters which will be set only once */
    PFN_VPP_SETPARAMES pfnSetParames;
    PFN_VPP_SETBASE pfnSetBase;
    PFN_VPP_SETSIZE pfnSetSize;    
    PFN_VPP_START pfnStart;
    PFN_VPP_STOP pfnStop;
    PFN_VPP_ISBUSY pfnIsBusy;
    PFN_VPP_SETINTERLACE pfnSetInterlace;
    PFN_VPP_CLEARDMAINTERRUPT pfnClearDMAInterrupt;
    PFN_VPP_ENABLEDMAINTERRUPT pfnEnableDMAInterrupt;
    PFN_VPP_DISABLEDMAINTERRUPT pfnDisableDMAInterrupt;
    PFN_VPP_ISDMAINTERRUPTED pfnIsDMAInterrupted;
    PFN_VPP_UPDATEFORMATENDIAN pfnUpdateFormatEndian;

    PFN_VPP_SLEEP pfnSleep;
    PFN_VPP_WAKEUP pfnWakeup;
    PFN_VPP_SETCOLORCTRL pfnSetColorCtrl;
    PFN_VPP_UPDATEHUE pfnUpdateHue;
    PFN_VPP_UPDATESATURATION pfnUpdateSaturation;
    PFN_VPP_UPDATEBRIGHT pfnUpdateBright;
    PFN_VPP_UPDATECONTRAST pfnUpdateContrast;
    PFN_VPP_UPDATECOEFF pfnUpdateCoeff;
    PFN_VPP_UPDATECOEFF2 pfnUpdateCoeff2;
    PFN_VPP_UPDATEYUV2RGB pfnUpdateYUV2RGB;
    PFN_VPP_PRINTREGISTER pfnPrintRegister;

    /*Internal debug function*/
    PFN_VPP_GETSOURCEINFO pfnGetSourceInfo;
    PFN_VPP_GETSOURCEBUFFER pfnGetSourceBuffer;
    PFN_VPP_GETDESTINFO pfnGetDestInfo;
    PFN_VPP_GETDESTBUFFER pfnGetDestBuffer;
    PFN_VPP_SETUSERMODE pfnSetUserMode;
    PFN_VPP_RESET pfnReset;
} VPP_FUNCTIONTABLE;

/***************************************************************************
** 
** Declare Fuction
****************************************************************************/

VOID VPP_GetFuncTable(VPP_FUNCTIONTABLE *pTable);
typedef VOID (*PFNVPP_GETFUNCTABLE)(VPP_FUNCTIONTABLE *pTable);

#if defined(__cplusplus)
}
#endif

#endif
