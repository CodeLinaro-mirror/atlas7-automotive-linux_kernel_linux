
/*
 * CSR hypervisor-call interfaces.
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#ifndef _LINUX_CSRVISOR_SYSCALLS_H_
#define _LINUX_CSRVISOR_SYSCALLS_H_

/*
 * CSRVISOR System Monitor Calls
 */
#define T_SMC_SWITCH  0		/* switch to NT */
#define T_SMC_PROF_INIT  2	/* initialize profile data */

#define T_SMC_FIFO_WRITE 5	/* write to fifo */
#define T_SMC_FIFO_READ  6	/* read from fifo */
#define T_SMC_FIFO_REG_WRITE 7	/* fifo register write */
#define T_SMC_FIFO_REG_READ 8	/* fifo register read */
/* set interrupt set-pending register address */
#define T_SMC_SET_ISPR_ADDR 0x10
#define T_SMC_NT_RESUME 0x11

#define NT_SMC_FIFO_WRITE 3	/* write to fifo */
#define NT_SMC_FIFO_READ  4	/* read from fifo */
#define NT_SMC_FIFO_REG_WRITE 5	/* fifo register write */
#define NT_SMC_FIFO_REG_READ 6	/* fifo register read */
#define NT_SMC_STEAL_GET 0x10   /* get stolen time */
#define T_SMC_NT_SUSPEND 0x11

#ifndef __ASSEMBLY__

struct csrvisor_fifo_msg;
struct csrvisor_fifo_io_req;

#ifdef CONFIG_SECURITY_MODE

static inline void csrvisor_set_ispr(unsigned long ispr_addr)
{
	register unsigned long r0 asm("r0") = T_SMC_SET_ISPR_ADDR;
	register unsigned long r1 asm("r1") = ispr_addr;

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");
}

static inline void csrvisor_fifo_write(struct csrvisor_fifo_msg *msg)
{
	register unsigned long r0 asm("r0") = T_SMC_FIFO_WRITE;
	register unsigned long r1 asm("r1") = (unsigned long)msg;

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");
}

static inline void csrvisor_fifo_read(struct csrvisor_fifo_msg *msg)
{
	register unsigned long r0 asm("r0") = T_SMC_FIFO_READ;
	register unsigned long r1 asm("r1") = (unsigned long)msg;

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");
}

static inline void csrvisor_fifo_reg_write(struct csrvisor_fifo_io_req *req)
{
	register unsigned long r0 asm("r0") = T_SMC_FIFO_REG_WRITE;
	register unsigned long r1 asm("r1") = (unsigned long)req;

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");
}

static inline void csrvisor_fifo_reg_read(struct csrvisor_fifo_io_req *req)
{
	register unsigned long r0 asm("r0") = T_SMC_FIFO_REG_READ;
	register unsigned long r1 asm("r1") = (unsigned long)req;

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");
}

static inline void csrvisor_nt_resume(void)
{
	register unsigned long r0 asm("r0") = T_SMC_NT_RESUME;
	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0)
	: "memory");
}

#else

#define CP15_DCACHE_INVALIDATE_CLEAN() \
	__asm__ __volatile__ ("mcr p15, 0, %0, c7, c14, 0" : : "r"(0))

#define CSRVISOR_FASTCALL_BASE 0x80000000UL

static inline void csrvisor_dummy_fastcall(void)
{
	__asm__ __volatile__(".arch_extension sec\n\t"
		"mov r0, %0\n\t"
		"smc #0\n\t" :
		: "I"(CSRVISOR_FASTCALL_BASE)
		: "r0", "memory");
}

static inline void csrvisor_nt_suspend(unsigned long func)
{
	register unsigned long r0 asm("r0") = T_SMC_NT_SUSPEND;
	register unsigned long r1 asm("r1") = (unsigned long)func;
	CP15_DCACHE_INVALIDATE_CLEAN();
	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :                      /* no output */
	: "r"(r0), "r"(r1)
	: "memory");
	CP15_DCACHE_INVALIDATE_CLEAN();
}

static inline void csrvisor_fifo_write(struct csrvisor_fifo_msg *msg)
{
	register unsigned long r0 asm("r0") = NT_SMC_FIFO_WRITE;
	register unsigned long r1 asm("r1") = (unsigned long)msg;

	CP15_DCACHE_INVALIDATE_CLEAN();

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");

	CP15_DCACHE_INVALIDATE_CLEAN();
}

static inline void csrvisor_fifo_read(struct csrvisor_fifo_msg *msg)
{
	register unsigned long r0 asm("r0") = NT_SMC_FIFO_READ;
	register unsigned long r1 asm("r1") = (unsigned long)msg;

	CP15_DCACHE_INVALIDATE_CLEAN();

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");

	CP15_DCACHE_INVALIDATE_CLEAN();
}

static inline void csrvisor_fifo_reg_write(struct csrvisor_fifo_io_req *req)
{
	register unsigned long r0 asm("r0") = NT_SMC_FIFO_REG_WRITE;
	register unsigned long r1 asm("r1") = (unsigned long)req;

	CP15_DCACHE_INVALIDATE_CLEAN();

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");

	CP15_DCACHE_INVALIDATE_CLEAN();
}

static inline void csrvisor_fifo_reg_read(struct csrvisor_fifo_io_req *req)
{
	register unsigned long r0 asm("r0") = NT_SMC_FIFO_REG_READ;
	register unsigned long r1 asm("r1") = (unsigned long)req;

	CP15_DCACHE_INVALIDATE_CLEAN();

	__asm__ __volatile__(".arch_extension sec\n\t"
	"smc #0" :			/* no output */
	: "r"(r0), "r"(r1)
	: "memory");

	CP15_DCACHE_INVALIDATE_CLEAN();
}
#endif /* CONFIG_SECURITY_MODE */

#endif /* __ASSEMBLY__ */

#endif /* _LINUX_CSRVISOR_SYSCALLS_H_ */
