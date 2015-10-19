#ifndef _PS_H
#define _PS_H

#define PS_FLUSH_REQ				0x001C
#define PS_FLUSH_RSP				0x101C

int ps_init(void);
void ps_ptr_update(void);

#endif /* _PS_H */
