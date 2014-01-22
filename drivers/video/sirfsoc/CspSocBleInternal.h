/*
 * CSR sirfsoc BLE internal interface
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __CSP_SOC_BLE_INTERNAL_H__
#define __CSP_SOC_BLE_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "CspCmnLcd.h"
#include "CspCmnBle.h"

/***************************************************************************
**
** Blt Engine Command Declare Area
****************************************************************************/

/*****************************************
     Command OP Code define
     Bit31   Bit30   Bit29  Bit28
         0       0       0      0   //SKIP_COMMAND
         0       0       1      0   //FENCE_WRITE
         0       0       1      1   //FENCE_WRITE With Interrupt Enbled
         0       1       0      0   //FENCE_WAIT
         0       1       1      0   //SET_REGISTER
         Others reserved
*******************************************/

#define OP_SKIP_COMMAND 0x0
#define OP_FENCE_WRITE  0x1
#define OP_FENCE_WAIT   0x2
#define OP_SET_REGISTER 0x3
#define OP_FENCE_WRITE_INTERRUPT    0x4


typedef union
{
    struct
    {
        UINT32 StartOffset  :8;
        UINT32 RegFollowed  :8;
        UINT32 Reserved     :13;
        UINT32 Header       :3;
    }set_register;

    struct
    {
        UINT32 CmdSkipped   :14;
        UINT32 Reserved     :15;
        UINT32 Header       :3;
    }skip;

    struct
    {
        UINT32 FenceAddr    :29;
        UINT32 Header       :3;
    }fence;

    UINT32 value;
}COMMAND;

INLINE UINT32 SET_REGISTER_BLE2D(UINT32 StartOffset, UINT32 nreg)
{
    COMMAND cmd;

    cmd.value = 0;
    cmd.set_register.StartOffset    = StartOffset;
    cmd.set_register.RegFollowed    = nreg;
    cmd.set_register.Header         = OP_SET_REGISTER;

    return cmd.value;
}

/***************************************************************************
**
** Blt Engine Register Declare Area
****************************************************************************/
#define FB_BASE                 0x00

#define ENG_STATUS              0x04
#define ENG_CTRL                0x08

#define RB_OFFSET               0x0C
#define RB_LENGTH               0x10
#define RB_RD_PTR               0x14
#define RB_WR_PTR               0x18

#define DST_OFFSET              0x1C
#define DST_FORMAT              0x20
#define DST_LT                  0x24
#define DST_RB                  0x28
#define CLIP_LT                 0x2C
#define CLIP_RB                 0x30
#define SRC_OFFSET              0x34
#define SRC_FORMAT              0x38
#define SRC_LT                  0x3C
#define SRC_RB                  0x40
#define PAT_OFFSET              0x44

#define FILL_COLOR              0x48
#define COLOR_KEY               0x4C
#define GBL_ALPHA               0x50

#define DRAW_CTL                0x54

//0x58~0x7c reserved
#define INTERRUPT_ENABLE        0x80
#define INTERRUPT_CLEAR         0x84
#define INTERRUPT_STATUS        0x88
#define HW_RESERVED             0x8C

#define MAX_REG_COUNT           0x40
#define BOUNDARY                0x18

#define VALID_INTERRUPT_MASK    0x0000000F

#define BLT_COMPLETE_INTERRUPT  0x00000000
#define BLT_TIMEOUT_INTERRUPT   0x00000001
#define FENCE_INTERRUPT         0x00000002
#define CMD_BUF_EMPTY_INTERRUPT 0x00000003

typedef union
{
    struct
    {
        UINT32 Address          :32;
    };
    UINT32   value;
} REG_FB_BASE;


typedef union
{
    struct
    {
        UINT32   RbEnabled      :1;     //rw
        UINT32   Idle           :1;     //rw
        UINT32   Reserved       :30;
    };
    UINT32   value;
} REG_ENG_STATUS;

typedef union
{
    struct
    {
        UINT32   RbEnable       :1;     //rw
        UINT32   sw_reset       :1;     //rw
        UINT32   Reserved       :30;
    };
    UINT32   value;
} REG_ENG_CTRL;

typedef union
{
    struct
    {
        UINT32   CmdBufEmpty    :1;
        UINT32   Fence          :1;
        UINT32   BltTimeOut     :1;
        UINT32   BltComplete    :1;
        UINT32   Reserved       :28;
    };
    UINT32 value;

} REG_INTERRUPT_CLEAR;

typedef union
{
    struct
    {
        UINT32   CmdBufEmpty    :1;
        UINT32   Fence          :1;
        UINT32   BltTimeOut     :1;
        UINT32   BltComplete    :1;
        UINT32   Reserved       :28;
    };
    UINT32 value;

} REG_INTERRUPT_ENABLE;

typedef union
{
    struct
    {
        UINT32   CmdBufEmpty    :1;
        UINT32   Fence          :1;
        UINT32   BltTimeOut     :1;
        UINT32   BltComplete    :1;
        UINT32   Reserved       :28;
    };
    UINT32 value;

} REG_INTERRUPT_STATUS;


typedef union
{
    struct
    {
        UINT32 Offset           :32;
    };
    UINT32   value;
} REG_RB_OFFSET;

typedef union
{
    struct
    {
        UINT32 Length           :16;
        UINT32 Reserved         :16;
    };
    UINT32   value;
} REG_RB_LENGTH;

typedef union
{
    struct
    {
        UINT32 Offset           :16;
        UINT32 Reserved         :16;
    };
    UINT32   value;
} REG_RB_RD_PTR,REG_RB_WR_PTR;

typedef union
{
    struct
    {
        UINT32   Offset         :32;
    };
    UINT32   value;
} REG_DST_OFFSET,REG_SRC_OFFSET,REG_PAT_OFFSET;

typedef union
{
    struct
    {
        UINT32   Stride         :14;
        UINT32   Reserved1      :2;
        UINT32   Format         :4;
        UINT32   Reserved2      :12;
    };
    UINT32   value;
} REG_DST_FORMAT;

typedef union
{
    struct
    {
        UINT32   Stride         :14;
        UINT32   Reserved1      :2;
        UINT32   Format         :4;
        UINT32   Non_Premuled   :1;
        UINT32   Reserved2      :11;
    };
    UINT32   value;
} REG_SRC_FORMAT;

typedef union
{
    struct
    {
        UINT32   Rop3           :8;
        UINT32   Reserved       :8;
        UINT32   ColorFill      :1;
        UINT32   Alpha          :2;
        UINT32   Flip_H         :1;
        UINT32   Flip_V         :1;
        UINT32   Rotation       :2;
        UINT32   Clip           :1;
        UINT32   Transp_enable  :1;
        UINT32   ColorKeyMode   :1;
        UINT32   Reserved1      :6;
    };
    UINT32   value;
} REG_DRAW_CTL;

typedef union
{
    struct
    {
        UINT32   B              :8;
        UINT32   G              :8;
        UINT32   R              :8;
        UINT32   A              :8;
    };
    UINT32   value;
} REG_FILL_COLOR,REG_COLOR_KEY;

typedef union
{
    struct
    {
        UINT32   Alpha          :8;
        UINT32   Reserved       :24;
    };
    UINT32   value;
} REG_GBL_ALPHA;

typedef union
{
    struct
    {
        INT32   Left            :12;
        INT32   Reserved1       :4;
        INT32   Top             :12;
        INT32   Reserved2       :4;
    };
    UINT32   value;
} REG_DST_LT,REG_SRC_LT,REG_CLIP_LT;

typedef union
{
    struct
    {
        INT32   Right           :12;
        INT32   Reserved1       :4;
        INT32   Bottom          :12;
        INT32   Reserved2       :4;
    };
    UINT32   value;
} REG_DST_RB,REG_SRC_RB,REG_CLIP_RB;

typedef struct
{
    REG_FB_BASE             reg_fb_base;
    REG_ENG_STATUS          reg_eng_status;
    REG_ENG_CTRL            reg_eng_ctrl;

    REG_RB_OFFSET           reg_rb_base;
    REG_RB_LENGTH           reg_rb_length;
    REG_RB_RD_PTR           reg_rb_read;
    REG_RB_WR_PTR           reg_rb_write;

    REG_DST_OFFSET          reg_dst_offset;
    REG_DST_FORMAT          reg_dst_format;
    REG_DST_LT              reg_dst_lt;
    REG_DST_RB              reg_dst_rb;
    REG_CLIP_LT             reg_clip_lt;
    REG_CLIP_RB             reg_clip_rb;

    REG_SRC_OFFSET          reg_src_offset;
    REG_SRC_FORMAT          reg_src_format;
    REG_SRC_LT              reg_src_lt;
    REG_SRC_RB              reg_src_rb;

    REG_PAT_OFFSET          reg_pat_offset;

    REG_FILL_COLOR          reg_fill_color;
    REG_COLOR_KEY           reg_color_key;
    REG_GBL_ALPHA           reg_gbl_alpha;

    REG_DRAW_CTL            reg_draw_ctrl;
    REG_INTERRUPT_CLEAR     reg_intr_clear;
    REG_INTERRUPT_ENABLE    reg_intr_enable;
    REG_INTERRUPT_STATUS    reg_intr_status;
} BLE2D_REGISTERS;

/***************************************************************************
**
** Debug Fuction Declare
****************************************************************************/
#ifndef BLE_DEBUG
#define BLE_DEBUG 0
#endif
#define BLEOutputMsg(fmt, args...) printk("BLE: " fmt, ##args)

#if BLE_DEBUG
#define BLE_MSG(X)  BLEOutputMsg X
#else
#define BLE_MSG(X)
#endif
#define BLE_ERR(X)  BLEOutputMsg X

#define BLE_MAX_DEBUG_MESSAGE_LEN 512

#if defined(__cplusplus)
}
#endif

#endif
