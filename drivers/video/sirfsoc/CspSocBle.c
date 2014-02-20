/*
 * CSR sirfsoc BLE library
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include "CspSocBleInternal.h"
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define WRITE_MMIO 1
#define READ_MMIO 2
static BOOL bFirstCmd = TRUE;

INT EGPEFormatToSrcBLE2D[] =
{
    -1,                     //gpe1Bpp
    -1,                     //gpe2Bpp
    -1,                     //gpe4Bpp
    -1,                     //gpe8Bpp
    BLE2D_RGB565,           //gpe16Bpp
    -1,                     //gpe24Bpp
    BLE2D_ARGB8888,         //gpe32Bpp
    -1,                     //gpe16YrCb
    -1,                     //gpeDeviceCompatible
    -1                      //gpeUndefined
};

INT EGPEFormatToDstBLE2D[] =
{
    -1,                     //gpe1Bpp
    -1,                     //gpe2Bpp
    -1,                     //gpe4Bpp
    -1,                     //gpe8Bpp
    BLE2D_RGB565,           //gpe16Bpp
    -1,                     //gpe24Bpp
    BLE2D_ARGB8888,         //gpe32Bpp
    -1,                     //gpe16YrCb
    -1,                     //gpeDeviceCompatible
    -1                      //gpeUndefined
};

BLE2DCONTEXT *pGBleContext = NULL;
BOOL bWaitBltComplete = FALSE;


void PrintCmd(void * addr,UINT32 length)
{
    char   *pAddr = (char*)addr;
    UINT32 i;


//    RETAILMSG(1,(TEXT("-------------------------Cmd-----------------------\n")));

    for (i = 0; i < length; i++)
    {
        BLE_MSG(("0x%.8x\n", *(UINT32*)pAddr));
        pAddr += 4;
    }

//    RETAILMSG(1,(TEXT("--------------------------------------------------")));

    return;
}

static INLINE UINT32 ReadBleRegister(BLE2DCONTEXT *pBle2DContext,UINT32 ui32Offset)
{
    return (*(volatile UINT32 * const)(pBle2DContext->pBleRegs + ui32Offset));
}

static INLINE VOID WriteBleRegister(BLE2DCONTEXT *pBle2DContext,UINT32 ui32Offset,UINT32 ui32Value)
{
	*(volatile UINT32 * const)(pBle2DContext->pBleRegs + ui32Offset) = ui32Value;
}

VOID PrintRingBufInfo(BLE2DCONTEXT *pBle2DContext, UINT32 SubmitSize)
{
    UINT32 i;
    BLE_MSG(("RingBufSizeInDW              = 0x%.8x\n", pBle2DContext->Mode.CmdMode.RingBuf.RingBufSizeInDW)); //RingBufSizeInDW
    BLE_MSG(("RingBufOffset                = 0x%.8x\n", pBle2DContext->Mode.CmdMode.RingBuf.RingBufOffset));
    BLE_MSG(("RingBufVirtualAddr           = 0x%.8x\n", pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual));
    BLE_MSG(("RingBufWritePtrUsedByDriver  = 0x%.8x\n", pBle2DContext->Mode.CmdMode.pRingBufWtPtr));
    BLE_MSG(("RingBufSizeLeftInDW          = 0x%.8x\n", pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW));
    BLE_MSG(("SubmitSize                   = 0x%.8x\n", SubmitSize));

    for(i = 0; i <= (DRAW_CTL + 0xC); i += 4)
    {
        BLE_MSG(("R%.8x     = %.8x\n", i, ReadBleRegister(pBle2DContext,i)));
    }
}

UINT32* __BleSoc_GetRingBufSpace(BLE2DCONTEXT *pBle2DContext, UINT32 SubmitSize)
{
    UINT32  SkippedCmd = 0;
    UINT32  RingBufRdPtr = 0;
    UINT32  RingBufWtPtr =0;

    if (SubmitSize >= (pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW))
    {
        RingBufRdPtr = ReadBleRegister(pBle2DContext,RB_RD_PTR);

        // Wrap around
        if ((pBle2DContext->Mode.CmdMode.pRingBufWtPtr + SubmitSize) >= ((UINT32*)pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual + pBle2DContext->Mode.CmdMode.RingBuf.RingBufSizeInDW))
        {
            INT32 temp1 = (INT32)pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW;
            INT32 temp2 = (INT32)SubmitSize;
            // Wait for enough RingBuffer space
            while (temp2 >= (INT32)(temp1 - RINGBUFFULLGAP))
            {
                RingBufWtPtr = ((UINT32)pBle2DContext->Mode.CmdMode.pRingBufWtPtr - pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual)/4;

                if(RingBufRdPtr > RingBufWtPtr)
                {
                    RingBufRdPtr = ReadBleRegister(pBle2DContext,RB_RD_PTR);
                }

                while(RingBufRdPtr > RingBufWtPtr)
                {
                    RingBufRdPtr = ReadBleRegister(pBle2DContext,RB_RD_PTR);
                }

                RingBufRdPtr = ReadBleRegister(pBle2DContext,RB_RD_PTR);
                pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW = RingBufRdPtr;

                temp1 = (INT32)pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW;
            }

            SkippedCmd = (pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual + pBle2DContext->Mode.CmdMode.RingBuf.RingBufSizeInDW*4
                        - (UINT32)pBle2DContext->Mode.CmdMode.pRingBufWtPtr)/4;

            if (SkippedCmd)
            {
                memset(pBle2DContext->Mode.CmdMode.pRingBufWtPtr, 0, SkippedCmd*sizeof(UINT32));
            }

            //reset pointer to the beginning of RingBuffer
            pBle2DContext->Mode.CmdMode.pRingBufWtPtr = (UINT32*)pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual;

            if(SubmitSize >= pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW)
            {
                BLE_ERR(("2dError:Not Enough RingBuffer Space,RingBufSizeLeftInDW = 0x%.8x,SubmitSize = 0x%.8x\n", pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW,SubmitSize));
		return NULL;
            }
        }
        else
        {
            RingBufWtPtr = ((UINT32)pBle2DContext->Mode.CmdMode.pRingBufWtPtr - pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual)/4;

            while( RingBufRdPtr > RingBufWtPtr)
            {
                RingBufRdPtr = ReadBleRegister(pBle2DContext,RB_RD_PTR);
            }

            pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW = pBle2DContext->Mode.CmdMode.RingBuf.RingBufSizeInDW- RingBufWtPtr;

            if(SubmitSize >= pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW)
            {
                BLE_ERR(("2dError:Not Enough RingBuffer Space,RingBufSizeLeftInDW = 0x%.8x,SubmitSize = 0x%.8x\n", pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW,SubmitSize));
		return NULL;
            }
        }
    }

    return pBle2DContext->Mode.CmdMode.pRingBufWtPtr ;
}

VOID __BleSoc_ReleaseRingBufSpace(BLE2DCONTEXT *pBle2DContext, UINT32 SubmitSize)
{
    UINT32  RingBufWrPtr = 0;

    pBle2DContext->Mode.CmdMode.RingBufSizeLeftInDW -= SubmitSize;
    pBle2DContext->Mode.CmdMode.pRingBufWtPtr       += SubmitSize;

    RingBufWrPtr = ((UINT32)pBle2DContext->Mode.CmdMode.pRingBufWtPtr - pBle2DContext->Mode.CmdMode.RingBuf.RingBufVirtual)/4;
   /*
	add wmb to make sure all the commands have already write
	in the ringbuffer before ble run */
	wmb();
    if(RingBufWrPtr < pBle2DContext->Mode.CmdMode.RingBuf.RingBufSizeInDW)
    {
        WriteBleRegister(pBle2DContext,RB_WR_PTR, RingBufWrPtr);
    }
    else
    {
        BLE_ERR(("RingBufWrPtr Out Range\n"));
    }
}

/*****************************************/
/* Only Used in MMIO Mode                */

VOID __BleSoc_SubmitCommandToHW(BLE2DCONTEXT *pBle2DContext, BLE2D_REGISTERS *pBle2DRegs)
{
    UINT32 i = 0;
    UINT32 *pData;

    pData = (UINT32*)pBle2DRegs + DST_OFFSET/4;

    for(i = DST_OFFSET; i <= DRAW_CTL; i += 4)
    {
        if(*pData != 0xFFFFFFFF)
        {
            WriteBleRegister(pBle2DContext,i,*pData);
        }

        pData++;
    }

    return;
}

//Add Timeout for the wait and check the engine status??
BLE2DERROR __BleSoc_WaitBltComplete(BLE2DCONTEXT *pBle2DContext, BLE2DMEMINFO *pMemInfo, BOOL bWaitForComplete)
{
    SYNC_OBJECT *pSyncObject;
    UINT32      CurrentSyncID;
    UINT32      Counter = 0;
    BOOL        bPrint = TRUE;

    if(!pMemInfo)
    {
        return  BLE2DERROR_MEMORY_UNAVAILABLE;
    }

    pSyncObject   = pMemInfo->pSyncObject;

    if(pBle2DContext->ble2dOPMode == BLE2DCOMMANDMODE)
    {
        CurrentSyncID = *(UINT32*)(pSyncObject->VirAddr);

        if(CurrentSyncID >= pMemInfo->ulDesiredSyncID || ((INT32)(pMemInfo->ulDesiredSyncID - CurrentSyncID) > SYNCOBJECTGAP))
        {
            return BLE2D_OK;
        }
        else
        {
            if(!bWaitForComplete)
            {
                return BLE2DERROR_BLT_NOTCOMPLETE;
            }
            else
            {
                while(1)
                {
                    CurrentSyncID = *(UINT32*)(pSyncObject->VirAddr);
                    Counter++;
                    if(CurrentSyncID >= pMemInfo->ulDesiredSyncID || ((INT32)(pMemInfo->ulDesiredSyncID - CurrentSyncID) > SYNCOBJECTGAP))
                    {
                        break;
                    }

                    if (Counter > 1000)
                    {
                        if(bPrint)
                        {
                            BLE_ERR(("2dError:Wait Fence Back Timeout,DesiredSyncID = 0x%.8x,ReadID = 0x%.8x\n", pMemInfo->ulDesiredSyncID,CurrentSyncID));
                            bPrint = FALSE;
                        }
                    }
			usleep_range(1500, 2000);
                }
            }
        }
    }
    else
    {
        CurrentSyncID = pBle2DContext->SyncObject.ulCurrentSyncID;
        if(CurrentSyncID >= pMemInfo->ulDesiredSyncID || ((INT32)(pMemInfo->ulDesiredSyncID - CurrentSyncID) > SYNCOBJECTGAP))
        {
            return BLE2D_OK;
        }
        else
        {
            if(!bWaitForComplete)
            {
                return BLE2DERROR_BLT_NOTCOMPLETE;
            }
            else
            {
                while(1)
                {
                    if(CurrentSyncID >= pMemInfo->ulDesiredSyncID || ((INT32)(pMemInfo->ulDesiredSyncID - CurrentSyncID) > SYNCOBJECTGAP))
                    {
                        break;
                    }
			usleep_range(1500, 2000);
                }
            }
        }
    }

    return BLE2D_OK;
}

UINT32 CalcIntersection(BLE2DRECTL *prclDst, BLE2DRECTL *prclClip, UINT32 NumClipRects)
{
    UINT32 nIntersection = 0;
    BLE2DRECTL *prclOut;

    for(prclOut = prclClip;NumClipRects>0;prclClip++,NumClipRects--)
    {
        prclOut->left  = max(prclDst->left , prclClip->left);
        prclOut->right = min(prclDst->right, prclClip->right);

        if(prclOut->left < prclOut->right)
        {
            prclOut->top    = max(prclDst->top   , prclClip->top);
            prclOut->bottom = min(prclDst->bottom, prclClip->bottom);
            if(prclOut->top < prclOut->bottom)
            {
                prclOut++;
                nIntersection++;
            }
        }
    }
    return nIntersection;
}

BLE2DERROR __BleSoc_PatternSurfControl(BLE2DCONTEXT    *pBle2DContext , BLE2DBLTINFO *pBltInfo, BLE2DMEMINFO **pMemInfo)
{
    UINT32 i = 0;
    UINT32 CurSyncId = 0;
    BOOL   bFound = FALSE;

    //Get Pattern Surface allocation
    for(i = pBle2DContext->CurPatBufIndex; i < MAX_PATTERN_BUF_RESERVED; i++)
    {
        if(pBle2DContext->pReservedPatSurf[i]->pSyncObject == NULL)
        {
            *pMemInfo = pBle2DContext->pReservedPatSurf[i];
            pBle2DContext->CurPatBufIndex = i + 1;
            bFound = TRUE;
            break;
        }
        else
        {
            if(pBle2DContext->ble2dOPMode == BLE2DCOMMANDMODE)
            {
                //read the sync id, and compare it with the sync object in the meminfo
                CurSyncId = *(UINT32*)pBle2DContext->pReservedPatSurf[i]->pSyncObject->VirAddr;
            }
            else
            {
                CurSyncId = pBle2DContext->SyncObject.ulCurrentSyncID;
            }

            if(CurSyncId >= pBle2DContext->pReservedPatSurf[i]->ulDesiredSyncID ||
                (pBle2DContext->pReservedPatSurf[i]->ulDesiredSyncID - CurSyncId) > SYNCOBJECTGAP)
            {
                *pMemInfo = pBle2DContext->pReservedPatSurf[i];
                pBle2DContext->CurPatBufIndex = i + 1;
                bFound = TRUE;
                break;
            }
        }
    }

    if(!bFound)
    {
        for(i = 0; i < pBle2DContext->CurPatBufIndex; i++)
        {
            if(pBle2DContext->pReservedPatSurf[i]->pSyncObject == NULL)
            {
                *pMemInfo = pBle2DContext->pReservedPatSurf[i];
                pBle2DContext->CurPatBufIndex = i+1;
                bFound = TRUE;
                break;
            }
            else
            {
                if(pBle2DContext->ble2dOPMode == BLE2DCOMMANDMODE)
                {
                    //read the sync id, and compare it with the sync object in the meminfo
                    CurSyncId = *(UINT32*)pBle2DContext->pReservedPatSurf[i]->pSyncObject->VirAddr;
                }
                else
                {
                    CurSyncId = pBle2DContext->SyncObject.ulCurrentSyncID;
                }

                if(CurSyncId >= pBle2DContext->pReservedPatSurf[i]->ulDesiredSyncID ||
                    (pBle2DContext->pReservedPatSurf[i]->ulDesiredSyncID - CurSyncId) > SYNCOBJECTGAP)
                {
                    *pMemInfo = pBle2DContext->pReservedPatSurf[i];
                    pBle2DContext->CurPatBufIndex = i+1;
                    bFound = TRUE;
                    break;
                }
             }
        }
    }

    if(!bFound)
    {
        return BLE2DERROR_GENERIC_ERROR;
    }
    else
    {
#if 0
        memcpy((void*)((*pMemInfo)->ulOffset + LCD_FRAME_BUF_VIRT_ADDR),
            (void*)(pBltInfo->pPatMemInfo->ulOffset + LCD_FRAME_BUF_VIRT_ADDR), BLE2D_PATTERN_SIZE);
#endif
    }

    return BLE2D_OK;
}


UINT32  __BleSoc_ClipCheck(BLE2DBLTINFO *pBltInfo, BLE2D_REGISTERS *pBLE2dregs,BLE2DRECTL *prclClip,UINT32 *pCmd)
{
    ULONG DrawCtrl = pBLE2dregs->reg_draw_ctrl.value;
    UINT32 ret = 0;

    DrawCtrl |= (1 << BLE2D_DRAWCTRL_CLIP_SHIFT);

    pBLE2dregs->reg_clip_lt.value = 0;
    pBLE2dregs->reg_clip_rb.value = 0;

    pBLE2dregs->reg_clip_lt.Left    = prclClip->left   & 0xFFF;
    pBLE2dregs->reg_clip_lt.Top     = prclClip->top    & 0xFFF;
    pBLE2dregs->reg_clip_rb.Right   = prclClip->right  & 0xFFF;
    pBLE2dregs->reg_clip_rb.Bottom  = prclClip->bottom & 0xFFF;

    pBLE2dregs->reg_draw_ctrl.value = DrawCtrl;
    if(pCmd)
    {
        *pCmd ++ = SET_REGISTER_BLE2D(CLIP_LT,2);
        *pCmd ++ = pBLE2dregs->reg_clip_lt.value;
        *pCmd ++ = pBLE2dregs->reg_clip_rb.value;

        ret += 3;
    }

    return ret;
}

UINT32 __BleSoc_SurfaceAndRectCheck(BLE2DBLTINFO *pBltInfo, BLE2D_REGISTERS *pBLE2dregs,UINT32 *pCmd)
{
    BOOL   bSrcExist = pBltInfo->bSrcExist;
    BOOL   bPatExist = pBltInfo->bPatExist;
    UINT32 ret = 0;

    if(bSrcExist)
    {
        pBLE2dregs->reg_src_offset.value      = pBltInfo->pSrcMemInfo->ulOffset;
	pBLE2dregs->reg_src_format.value = (pBltInfo->AlphaBlendFunc & 0x01) << 20 | pBltInfo->SrcFormat << 16 | pBltInfo->SrcStride;

        pBLE2dregs->reg_src_lt.value = 0;
        pBLE2dregs->reg_src_rb.value = 0;

        pBLE2dregs->reg_src_lt.Left     = pBltInfo->SrcX & 0xFFF;
        pBLE2dregs->reg_src_lt.Top      = pBltInfo->SrcY & 0xFFF;
        pBLE2dregs->reg_src_rb.Right    = (pBltInfo->SrcX + pBltInfo->SrcSizeX) & 0xFFF;
        pBLE2dregs->reg_src_rb.Bottom   = (pBltInfo->SrcY + pBltInfo->SrcSizeY) & 0xFFF;

        if(pCmd)
        {
            *pCmd++ = SET_REGISTER_BLE2D(SRC_OFFSET,4);
            *pCmd++ = pBLE2dregs->reg_src_offset.value;
            *pCmd++ = pBLE2dregs->reg_src_format.value;
            *pCmd++ = pBLE2dregs->reg_src_lt.value;
            *pCmd++ = pBLE2dregs->reg_src_rb.value;
            ret += 5;
        }
    }

    if(bPatExist)
    {
        pBLE2dregs->reg_pat_offset.value      = (ULONG)pBltInfo->pPatMemInfo->ulOffset;

        if(pCmd)
        {
            *pCmd++ = SET_REGISTER_BLE2D(PAT_OFFSET,1);
            *pCmd++ = pBLE2dregs->reg_pat_offset.value;
            ret += 2;
        }
    }

    pBLE2dregs->reg_dst_offset.value     = (ULONG)pBltInfo->pDstMemInfo->ulOffset;
    pBLE2dregs->reg_dst_format.value     = (pBltInfo->DstFormat<<16 | pBltInfo->DstStride);

    pBLE2dregs->reg_dst_lt.value         = 0;
    pBLE2dregs->reg_dst_rb.value         = 0;

    pBLE2dregs->reg_dst_lt.Left          = pBltInfo->DstX & 0xFFF;
    pBLE2dregs->reg_dst_lt.Top           = pBltInfo->DstY & 0xFFF;
    pBLE2dregs->reg_dst_rb.Right         = (pBltInfo->DstX + pBltInfo->DstSizeX) & 0xFFF;
    pBLE2dregs->reg_dst_rb.Bottom        = (pBltInfo->DstY + pBltInfo->DstSizeY) & 0xFFF;

    if(pCmd)
    {
        *pCmd++ = SET_REGISTER_BLE2D(DST_OFFSET,4);
        *pCmd++ = pBLE2dregs->reg_dst_offset.value;
        *pCmd++ = pBLE2dregs->reg_dst_format.value;

        *pCmd++ = pBLE2dregs->reg_dst_lt.value;
        *pCmd++ = pBLE2dregs->reg_dst_rb.value;

        ret += 5;

    }

    return ret;
}

UINT32  __BleSoc_AlphaBlendCheck(BLE2DBLTINFO *pBltInfo, BLE2D_REGISTERS *pBLE2dregs,UINT32 *pCmd)
{
    ULONG DrawCtrl = pBLE2dregs->reg_draw_ctrl.value;
    UINT32 ret = 0;

    DrawCtrl &= ~(3 << BLE2D_DRAWCTRL_ALPHA_SHIFT);

     //Both constant and perpixel alpha
    if((pBltInfo->BlitFlags & (BLE2D_BLIT_PERPIXEL_ALPHA | BLE2D_BLIT_GLOBAL_ALPHA)) == (BLE2D_BLIT_PERPIXEL_ALPHA|BLE2D_BLIT_GLOBAL_ALPHA))
    {
        DrawCtrl |= (3 << BLE2D_DRAWCTRL_ALPHA_SHIFT);
        pBLE2dregs->reg_gbl_alpha.value = pBltInfo->GlobalAlpha;
        if(pCmd)
        {
            *pCmd ++ = SET_REGISTER_BLE2D(GBL_ALPHA,1);
            *pCmd ++ =  pBLE2dregs->reg_gbl_alpha.value;
            ret += 2;
        }
    }
    else if(pBltInfo->BlitFlags & BLE2D_BLIT_PERPIXEL_ALPHA)//Perpixel only
    {
        DrawCtrl |= (2 << BLE2D_DRAWCTRL_ALPHA_SHIFT);
    }
    else if(pBltInfo->BlitFlags & BLE2D_BLIT_GLOBAL_ALPHA)//Constant only
    {
        DrawCtrl |= (1 << BLE2D_DRAWCTRL_ALPHA_SHIFT);
        pBLE2dregs->reg_gbl_alpha.value = pBltInfo->GlobalAlpha;

        if(pCmd)
        {
            *pCmd ++ = SET_REGISTER_BLE2D(GBL_ALPHA,1);
            *pCmd ++ =  pBLE2dregs->reg_gbl_alpha.value;
            ret += 2;
        }
    }

    pBLE2dregs->reg_draw_ctrl.value = DrawCtrl;

    return ret;
}

UINT32  __BleSoc_TransparentCheck(BLE2DBLTINFO *pBltInfo, BLE2D_REGISTERS *pBLE2dregs, UINT32 *pCmd)
{
    ULONG  DrawCtrl = pBLE2dregs->reg_draw_ctrl.value;
    UINT32 ret = 0;

    DrawCtrl &= ~(1 << BLE2D_DRAWCTRL_COLORKEY_MODE_SHIFT);

    if(pBltInfo->BlitFlags & BLE2D_BLIT_SRC_COLORKEY)
    {
        DrawCtrl |= (0 << BLE2D_DRAWCTRL_COLORKEY_MODE_SHIFT);
    }
    else
    {
        DrawCtrl |= (1 << BLE2D_DRAWCTRL_COLORKEY_MODE_SHIFT);
    }

    DrawCtrl |= (1 << BLE2D_DRAWCTRL_TRANSPARENT_SHIFT);

    pBLE2dregs->reg_color_key.value = pBltInfo->ColorKey;

    if(pCmd)
    {
        *pCmd++ = SET_REGISTER_BLE2D(COLOR_KEY,1);
        *pCmd++ = pBLE2dregs->reg_color_key.value;
        ret += 2;
    }

    pBLE2dregs->reg_draw_ctrl.value = DrawCtrl;
    return ret;
}


UINT32  __BleSoc_ColorFillCheck(BLE2DBLTINFO *pBltInfo, BLE2D_REGISTERS *pBLE2dregs,UINT32 *pCmd)
{
    ULONG DrawCtrl = pBLE2dregs->reg_draw_ctrl.value;
    UINT32 ret = 0;

    DrawCtrl |= (1 << BLE2D_DRAWCTRL_COLORFILL_SHIFT);

    pBLE2dregs->reg_fill_color.value = pBltInfo->FillColor;

    pBLE2dregs->reg_draw_ctrl.value = DrawCtrl;
    if(pCmd)
    {
        *pCmd++ = SET_REGISTER_BLE2D(FILL_COLOR,1);
        *pCmd++ = pBLE2dregs->reg_fill_color.value;
        ret += 2;
    }

    return ret;
}

VOID  __BleSoc_SwizzleCheck(BLE2DBLTINFO *pBltInfo, BLE2D_REGISTERS *pBLE2dregs)
{
    ULONG DrawCtrl = pBLE2dregs->reg_draw_ctrl.value;

    DrawCtrl &= ~(3 << BLE2D_DRAWCTRL_ROTATION_SHIFT | 1 << BLE2D_DRAWCTRL_FLIP_H_SHIFT | 1 << BLE2D_DRAWCTRL_FLIP_V_SHIFT);

    switch (pBltInfo->BlitFlags & (BLE2D_BLIT_ROT_90 | BLE2D_BLIT_ROT_180 | BLE2D_BLIT_ROT_270))
    {
        case BLE2D_BLIT_ROT_90:
            DrawCtrl |= (1 << BLE2D_DRAWCTRL_ROTATION_SHIFT);
            break;
        case BLE2D_BLIT_ROT_180:
            DrawCtrl |= (2 << BLE2D_DRAWCTRL_ROTATION_SHIFT);
            break;
        case BLE2D_BLIT_ROT_270:
            DrawCtrl |= (3 << BLE2D_DRAWCTRL_ROTATION_SHIFT);
            break;
        default:
            break;
    }

    if(pBltInfo->BlitFlags & BLE2D_BLIT_FLIP_H)
    {
        DrawCtrl |= (1 << BLE2D_DRAWCTRL_FLIP_H_SHIFT);
    }

    if(pBltInfo->BlitFlags & BLE2D_BLIT_FLIP_V)
    {
        DrawCtrl |= (1 << BLE2D_DRAWCTRL_FLIP_V_SHIFT);
    }

    pBLE2dregs->reg_draw_ctrl.value = DrawCtrl;

}

BLE2DERROR __Ble2D_Bitblt(BLE2DCONTEXT *pBle2DContext, BLE2DBLTINFO *pBltInfo, BLE2DRECTL *prclClip)
{
    BLE2D_REGISTERS  ble2dreg;
    BLE2D_REGISTERS *pBLE2dregs = &ble2dreg;
    UINT32 SubmitSize = 0;
    UINT32 BltCmd[BLE2D_MAX_BLIT_CMD_SIZE] = {0,};
    UINT32 CurCmdIndex = 0;
    BOOL   bMMIOMode = FALSE;
    VOID   *pBuf = NULL;
    BOOL bPrintCmd = FALSE;

    memset(pBLE2dregs, 0xFF,sizeof(BLE2D_REGISTERS));

    pBLE2dregs->reg_draw_ctrl.value = 0;
    pBLE2dregs->reg_draw_ctrl.Rop3 = pBltInfo->ROP3;

    if(pBle2DContext->ble2dOPMode == BLE2DMMIOMODE)
    {
        bMMIOMode = TRUE;
    }

    //Check rotation & mirror
    if(pBltInfo->BlitFlags & (BLE2D_BLIT_ROT_90 | BLE2D_BLIT_ROT_180 | BLE2D_BLIT_ROT_270 | BLE2D_BLIT_FLIP_H | BLE2D_BLIT_FLIP_V))
    {
        __BleSoc_SwizzleCheck(pBltInfo, pBLE2dregs);
    }

    if(!bMMIOMode)
    {
        if(pBltInfo->bNeedSyncLast && !bFirstCmd)
        {
           BltCmd[CurCmdIndex++] = (OP_FENCE_WAIT << 29 | pBle2DContext->SyncObject.PhyAddr >> 3);
           BltCmd[CurCmdIndex++] = pBle2DContext->SyncObject.ulCurrentSyncID - 1;;
        }
		else if(bFirstCmd)
		{
			bFirstCmd = FALSE;
		}
       //Check Clip
        if(pBltInfo->BlitFlags & BLE2D_BLIT_CLIP_ENABLE)
        {
		CurCmdIndex += __BleSoc_ClipCheck(pBltInfo, pBLE2dregs,prclClip,&BltCmd[CurCmdIndex]);
        }

        //Check Surface
        CurCmdIndex += __BleSoc_SurfaceAndRectCheck(pBltInfo,pBLE2dregs,&BltCmd[CurCmdIndex]);

        //Check Alpha Blend
        if (pBltInfo->BlitFlags & (BLE2D_BLIT_PERPIXEL_ALPHA | BLE2D_BLIT_GLOBAL_ALPHA))
        {
            CurCmdIndex += __BleSoc_AlphaBlendCheck(pBltInfo, pBLE2dregs,&BltCmd[CurCmdIndex]);
        }

        //Check transparent and color key
        if (pBltInfo->BlitFlags & BLE2D_BLIT_TRANSPARENT_ENABLE)
        {
            CurCmdIndex += __BleSoc_TransparentCheck(pBltInfo, pBLE2dregs, &BltCmd[CurCmdIndex]);
        }

        //Check color fill
        if(pBltInfo->BlitFlags & BLE2D_BLIT_COLOR_FILL)
        {
            CurCmdIndex += __BleSoc_ColorFillCheck(pBltInfo, pBLE2dregs,&BltCmd[CurCmdIndex]);
        }

        BltCmd[CurCmdIndex++] = SET_REGISTER_BLE2D(DRAW_CTL, 1);
        BltCmd[CurCmdIndex++] = pBLE2dregs->reg_draw_ctrl.value;

        BltCmd[CurCmdIndex++] = (OP_FENCE_WRITE_INTERRUPT << 29 | pBle2DContext->SyncObject.PhyAddr >> 3);
        BltCmd[CurCmdIndex++] = pBle2DContext->SyncObject.ulCurrentSyncID++;

        SubmitSize = CurCmdIndex;
        SubmitSize = ((SubmitSize +3) &~3);

        //Submit Command to RingBuf
        pBuf = (UCHAR*)__BleSoc_GetRingBufSpace(pBle2DContext,SubmitSize);

        if(bPrintCmd)
        {
            PrintCmd(BltCmd,SubmitSize);
        }

        memcpy(pBuf,BltCmd,SubmitSize*4);

        __BleSoc_ReleaseRingBufSpace(pBle2DContext,SubmitSize);

        if(bWaitBltComplete)
        {
            UINT32 counter = 0;
            while(1)
            {
                DWORD fenceid = (pBle2DContext->SyncObject.ulCurrentSyncID - 1);
                if(*(DWORD*)pBle2DContext->SyncObject.VirAddr >= fenceid)
                {
                    break;
                }
                counter++;

                if(counter == 0xFFFFFF)
                {
                    counter = 0;
                    BLE_ERR(("Engine hang\n"));
                    break;
                }
            }
        }
    }

    return BLE2D_OK;
}

BLE2DERROR __BleSoc_BitBlt(VOID *hContext, BLE2DBLTINFO *pBltInfo)
{
    BLE2DCONTEXT    *pBle2DContext = (BLE2DCONTEXT*)hContext;
    BLE2DRECTL      rclDst,rclSrc;
    ULONG           ulNumClipRects = pBltInfo->NumClipRect;
    BLE2DRECTL      *pClipRect = pBltInfo->pBleClipRect;
    ULONG           nClipRects;
    BLE2DRECTL      *prclClip = NULL;
    int             i;

    //Dest rect
    rclDst.left     = pBltInfo->DstX;
    rclDst.right    = rclDst.left + pBltInfo->DstSizeX;
    rclDst.top      = pBltInfo->DstY;
    rclDst.bottom   = rclDst.top + pBltInfo->DstSizeY;

    //Check Pattern
    if (pBltInfo->bPatExist)
    {
        BLE2DMEMINFO *pPatSurf = NULL;

        if(__BleSoc_PatternSurfControl(pBle2DContext, pBltInfo,&pPatSurf) != BLE2D_OK)
        {
            BLE_ERR(("Can't get available Pattern Surface\n"));
            return BLE2DERROR_GENERIC_ERROR;
        }

        pBltInfo->pPatMemInfo   = pPatSurf;
        pBltInfo->pPatMemInfo->pSyncObject      = &pBle2DContext->SyncObject;
        pBltInfo->pPatMemInfo->ulDesiredSyncID  = pBle2DContext->SyncObject.ulCurrentSyncID;
    }
    pBltInfo->pDstMemInfo->pSyncObject      = &pBle2DContext->SyncObject;
    pBltInfo->pDstMemInfo->ulDesiredSyncID  = pBle2DContext->SyncObject.ulCurrentSyncID;
    //Check Src
    if (pBltInfo->bSrcExist)
    {
        // Src rect
        rclSrc.left     = pBltInfo->SrcX;
        rclSrc.right    = rclSrc.left + pBltInfo->SrcSizeX;
        rclSrc.top      = pBltInfo->SrcY;
        rclSrc.bottom   = rclSrc.top + pBltInfo->SrcSizeY;
        pBltInfo->pSrcMemInfo->pSyncObject      = &pBle2DContext->SyncObject;
        pBltInfo->pSrcMemInfo->ulDesiredSyncID  = pBle2DContext->SyncObject.ulCurrentSyncID;
    }

    if(ulNumClipRects)
    {
        nClipRects = CalcIntersection(&rclDst,pClipRect,ulNumClipRects);

        if(nClipRects > 0)
        {
            pBltInfo->BlitFlags = (BLE2DBLITFLAGS)(pBltInfo->BlitFlags | BLE2D_BLIT_CLIP_ENABLE);
        }
        prclClip   = pClipRect;
    }
    else
    {
        nClipRects = 1;
        prclClip   = &rclDst;
    }

    for(i = 0; i < nClipRects; i++)
    {
        __Ble2D_Bitblt(pBle2DContext,pBltInfo,prclClip);

	prclClip++;
    }

    //Everything is clipped
    if(nClipRects == 0)
    {
        return BLE2D_OK;
    }

#ifdef SYNC_BLT
    __BleSoc_WaitBltComplete(pBle2DContext,pBltInfo->pDstMemInfo,1);
#endif

    return BLE2D_OK;
}

VOID __BleSoc_ClearInterrupt(BLE2DCONTEXT *pBle2DContext, UINT32 uiInterruptIndex)
{
    UINT32 reg_int_clear;

    reg_int_clear = (UINT32)(1<<uiInterruptIndex);

    WriteBleRegister(pBle2DContext,INTERRUPT_CLEAR,reg_int_clear);
}

VOID __BleSoc_EnableInterrupt(BLE2DCONTEXT *pBle2DContext, UINT32 uiInterruptIndex)
{
    UINT32 reg_int_enable;

    reg_int_enable = ReadBleRegister(pBle2DContext,INTERRUPT_ENABLE);

    reg_int_enable |= (1<<uiInterruptIndex);

    WriteBleRegister(pBle2DContext, INTERRUPT_ENABLE, reg_int_enable);
}

VOID __BleSoc_DisableInterrupt(BLE2DCONTEXT *pBle2DContext, UINT32 uiInterruptIndex)
{
    UINT32 reg_int_enable;

    reg_int_enable = ReadBleRegister(pBle2DContext,INTERRUPT_ENABLE);

    reg_int_enable &= ~(UINT32) (1<<uiInterruptIndex);

    WriteBleRegister(pBle2DContext,INTERRUPT_ENABLE, reg_int_enable);
}

BOOL __BleSoc_GetInterruptStatus(BLE2DCONTEXT *pBle2DContext, UINT32 uiIntertuptIndex)
{
    UINT32 reg_int_status = 0;

    reg_int_status = ReadBleRegister(pBle2DContext,INTERRUPT_STATUS);

    reg_int_status = (reg_int_status >> uiIntertuptIndex)& 0x1;

    return reg_int_status;
}

VOID __BleSoc_EnableClock(VOID)
{
}

VOID __BleSoc_DisableClock(VOID)
{
}

VOID __BleSoc_Reset(VOID)
{
}


VOID __BleSoc_Setup(VOID *pData)
{
    BLE2DCONTEXT *pBleContext = (BLE2DCONTEXT*)pData;

    __BleSoc_EnableClock();
    __BleSoc_Reset();

    if(!pBleContext)
    {
        return;
    }

    //Config FB base register
    WriteBleRegister(pBleContext, FB_BASE,  0);

    if(pBleContext->ble2dOPMode == BLE2DCOMMANDMODE)
    {
        pBleContext->Mode.CmdMode.pRingBufWtPtr              = (UINT32*)pBleContext->Mode.CmdMode.RingBuf.RingBufVirtual;
        pBleContext->Mode.CmdMode.RingBufSizeLeftInDW        = pBleContext->Mode.CmdMode.RingBuf.RingBufSizeInDW;

        WriteBleRegister(pBleContext, ENG_CTRL,  0x1);
        WriteBleRegister(pBleContext, RB_OFFSET, pBleContext->Mode.CmdMode.RingBuf.RingBufOffset);
        WriteBleRegister(pBleContext, RB_LENGTH, pBleContext->Mode.CmdMode.RingBuf.RingBufSizeInDW);
        WriteBleRegister(pBleContext, RB_RD_PTR, 0);
        WriteBleRegister(pBleContext, RB_WR_PTR, 0);

        __BleSoc_EnableInterrupt(pBleContext, FENCE_INTERRUPT);
    }
    else
    {
        __BleSoc_EnableInterrupt(pBleContext, BLT_COMPLETE_INTERRUPT);
    }

}

/*****************************************************************************/
/*                      Function Table  Area                                 */
/*****************************************************************************/

BOOL BleSoc_IsBusy(VOID)
{
    REG_ENG_STATUS    reg_eng_status;

    reg_eng_status.value = ReadBleRegister(pGBleContext, ENG_STATUS);

    return (reg_eng_status.Idle == 0);
}

VOID BleSoc_Wakeup(VOID)
{
    bFirstCmd	= TRUE;
   __BleSoc_Setup(pGBleContext);

   return;
}

VOID BleSoc_Sleep(VOID)
{
   UINT32 times = 0;

   while(BleSoc_IsBusy())
   {
        mdelay(1);
        times++;
        if (times > 20)
        {
            BLE_ERR(("Error: Blit engine is busy\n"));
            break;
        }
   }

   return;
}

BOOL BleSoc_Initialize(VOID **pBle2DContext, VOID *pInitData)
{
    BLE2DINITMEMINFO *pBle2DInitMemInfo = (BLE2DINITMEMINFO *)pInitData;
    BLE2DCONTEXT *pBleContext;
    BOOL  bCmdMode = TRUE;
    int i;

    pBleContext = (BLE2DCONTEXT *)kzalloc(sizeof(BLE2DCONTEXT), GFP_KERNEL);

    if(!pBleContext)
    {
        return FALSE;
    }

    pGBleContext = pBleContext;

    for(i = 0;i < MAX_PATTERN_BUF_RESERVED; i++)
    {
        pBleContext->pReservedPatSurf[i] = NULL;
    }

    pBleContext->CurPatBufIndex = 0;

    pBleContext->pBleRegs = pBle2DInitMemInfo->RegBase;

    if (bCmdMode)
    {
        pBleContext->Mode.CmdMode.RingBuf.RingBufOffset
            = (pBle2DInitMemInfo->MemOffset + RING_BUF_ALIGNMENT - 1) & (~(RING_BUF_ALIGNMENT - 1));
        pBleContext->Mode.CmdMode.RingBuf.RingBufSizeInDW = RING_BUF_SIZE / 4;
        pBleContext->Mode.CmdMode.RingBuf.RingBufVirtual
            = pBle2DInitMemInfo->MemBase + (pBleContext->Mode.CmdMode.RingBuf.RingBufOffset - pBle2DInitMemInfo->MemOffset);

        pBleContext->Mode.CmdMode.pRingBufWtPtr = (UINT32*)pBleContext->Mode.CmdMode.RingBuf.RingBufVirtual;
        pBleContext->Mode.CmdMode.RingBufSizeLeftInDW = pBleContext->Mode.CmdMode.RingBuf.RingBufSizeInDW;

        pBleContext->SyncObject.PhyAddr = (pBleContext->Mode.CmdMode.RingBuf.RingBufOffset
            + RING_BUF_SIZE + FENCE_BUF_ALIGNMENT - 1) & (~(FENCE_BUF_ALIGNMENT - 1)) ;
        pBleContext->SyncObject.VirAddr
            = pBle2DInitMemInfo->MemBase + (pBleContext->SyncObject.PhyAddr - pBle2DInitMemInfo->MemOffset);
        pBleContext->ble2dOPMode = BLE2DCOMMANDMODE;
        pBleContext->SyncObject.ulCurrentSyncID = 1;
    }

    //CspRegMap(FALSE);

    __BleSoc_Setup(pBleContext);
    BLE_MSG(("bWaitBltComplete = %x\n", bWaitBltComplete));

    *pBle2DContext = (VOID *)pBleContext;
    return TRUE;
}

BOOL BleSoc_Terminate(VOID *hContext)
{
    //CspRegUnMap();

    return TRUE;
}

BOOL BleSoc_CheckBltParams(VOID* pBltParams)
{
    //To Do, add Linux support
    return TRUE;;
}

BLE2DERROR BleSoc_QueryBltStatus(VOID *pBle2DContext, VOID *pMemInfo, BOOL bWait)
{
    return __BleSoc_WaitBltComplete((BLE2DCONTEXT*)pBle2DContext, (BLE2DMEMINFO*)pMemInfo, bWait);
}

UINT32 BleSoc_BitBlt(VOID *hContext, VOID *BltInfo)
{
    return __BleSoc_BitBlt(hContext, (BLE2DBLTINFO*)BltInfo);
}

VOID  BleSoc_InterruptRoutine(VOID *hContext)
{
    UINT32 IntrStatus = 0;
    UINT32 IntrEnabled = 0;
    UINT32 temp = 0;
    BLE2DCONTEXT  *pBleContext = (BLE2DCONTEXT*)hContext;
    static UINT32 FenceId = 0;

    IntrStatus  = ReadBleRegister(pBleContext, INTERRUPT_STATUS);
    IntrStatus &= VALID_INTERRUPT_MASK;

    IntrEnabled  = ReadBleRegister(pBleContext, INTERRUPT_ENABLE);
    IntrEnabled &= VALID_INTERRUPT_MASK;

    IntrStatus &= IntrEnabled;

    //Disalbe All Interrupt
    WriteBleRegister(pBleContext, INTERRUPT_ENABLE,0x0);

    if(IntrStatus & (1<< CMD_BUF_EMPTY_INTERRUPT))
    {
        __BleSoc_ClearInterrupt(pBleContext, CMD_BUF_EMPTY_INTERRUPT);
    }

    if(IntrStatus & (1<<FENCE_INTERRUPT))
    {
        temp = FenceId;

        __BleSoc_ClearInterrupt(pBleContext, FENCE_INTERRUPT);
        FenceId = *(UINT32*)(pBleContext->SyncObject.VirAddr);

        if(FenceId != (temp+1))
        {
            ;
            //RETAILMSG(1,(TEXT("Fence Lost, Last Fence ID = 0x%.8x, New Fence ID = 0x%.8x\n"),temp,FenceId));
        }

        if(temp > FenceId)
        {
            BLE_ERR(("Error Last Fence ID = 0x%.8x, New Fence ID = 0x%.8x\n", temp, FenceId));
            PrintRingBufInfo(pBleContext, 0);
        }

        if(FenceId%1000 == 0)
        {
            BLE_MSG(("FenceId = 0x%.8x is back\n", FenceId));
        }
    }

    if(IntrStatus & (1<<BLT_TIMEOUT_INTERRUPT))
    {
        __BleSoc_ClearInterrupt(pBleContext, BLT_TIMEOUT_INTERRUPT);
    }

    if(IntrStatus & (1<<BLT_COMPLETE_INTERRUPT))
    {
        __BleSoc_ClearInterrupt(pBleContext, BLT_COMPLETE_INTERRUPT);

    }

    //Enable All Interrupt
    WriteBleRegister(pBleContext, INTERRUPT_ENABLE,IntrEnabled);
}

VOID BleSoc_PrintRegisters(VOID)
{
    UINT32 i = 0;

    for(i = 0; i <= DRAW_CTL; i += 4)
    {
        BLE_MSG(("Register offset %.8x = %.8x\r\n", i, ReadBleRegister(NULL,i)));
    }
}

VOID BleSoc_EnableClock(VOID)
{
     __BleSoc_EnableClock();
}

VOID BleSoc_DisableClock(VOID)
{
     __BleSoc_DisableClock();
}

VOID BleSoc_Reset(VOID)
{
     __BleSoc_Reset();
}

VOID BleSoc_GetFuncTable(BLE_FUNCTIONTABLE *pTable)
{
    memset(pTable, 0, sizeof(BLE_FUNCTIONTABLE));

    pTable->pfnInitialize            = BleSoc_Initialize;
    pTable->pfnTerminate             = BleSoc_Terminate;
    pTable->pfnCheckBltParams        = BleSoc_CheckBltParams;
    pTable->pfnBitBlt                = BleSoc_BitBlt;
    pTable->pfnQueryBltStatus        = BleSoc_QueryBltStatus;
    pTable->pfnPrintRegisters        = BleSoc_PrintRegisters;
    pTable->pfnWakeup                = BleSoc_Wakeup;
    pTable->pfnSleep                 = BleSoc_Sleep;
    pTable->pfnInterruptRoutine      = BleSoc_InterruptRoutine;

    pTable->pfnEnableClock           = BleSoc_EnableClock;
    pTable->pfnDisableClock          = BleSoc_DisableClock;
    pTable->pfnReset                 = BleSoc_Reset;
}
