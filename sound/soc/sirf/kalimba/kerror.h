#ifndef _KERROR_H
#define _KERROR_H

#define KAS_ERROR_PANIC			0x0020
#define KAS_ERROR_FAULT			0x0021

#define KAS_CMD_SUCCESS			0x0000
#define KAS_CMD_FAILED			0x1000
#define KAS_CMD_INVALID			0x1001
#define KAS_CMD_NOT_SUPPORTED		0x1002
#define KAS_CMD_INVALID_ARGS		0x1003
#define KAS_CMD_INVALID_LENGTH		0x1004
#define KAS_CMD_INVALID_CONN_ID		0x1005

const char *kerror_str(u16 status);
int kcoredump_init(void);
int kcoredump(void);
#endif /* _KERROR_H */
