
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
#define	 T_SMC_SWITCH_OK	  0	/* no error */
#define	 T_SMC_SWITCH_ERR	 1	/* error, return to T */
#define	 T_SMC_SWITCH_HANDLER 2	/* NT returns, raise csrvisor handler */
#define	 T_SMC_SWITCH_RET	 3	/* NT returns */
#define	 T_SMC_SWITCH_SHVAR   4	/* NT returns, arg1 = shared variable */
#define	 T_SMC_SWITCH_VERIFY  5	/* obj_csrvisor_monitor */

#define T_SMC_REBOOT  1		/* reboot NT */
#define	 T_SMC_REBOOT_OK	  0	/* no error */
#define	 T_SMC_REBOOT_ERR	 1	/* error, return to T */

#define T_SMC_PROF_INIT  2	/* initialize profile data */
#define	 T_SMC_PROF_INIT_OK	  0	/* no error */
#define	 T_SMC_PROF_INIT_ERR	 1	/* error, return to T */

#define T_SMC_PROF_GET  3	/* get profile data */
#define	 T_SMC_PROF_GET_OK	  0	/* no error */
#define	 T_SMC_PROF_GET_ERR	 1	/* error, return to T */

#define T_SMC_PROF_GET_PTR  4	/* get profile data pointer */
#define	 T_SMC_PROF_GET_PTR_OK	  0	/* no error */
#define	 T_SMC_PROF_GET_PTR_ERR	 1	/* error, return to T */

#define T_SMC_FIFO_WRITE 5	/* write to fifo */
#define T_SMC_FIFO_READ  6	/* read from fifo */
#define T_SMC_FIFO_REG_WRITE 7	/* fifo register write */
#define T_SMC_FIFO_REG_READ 8	/* fifo register read */
/* set interrupt set-pending register address */
#define T_SMC_SET_ISPR_ADDR 0x10

#define NT_SMC_SWITCH 0		/* switch to T */
#define	 NT_SMC_SWITCH_OK	 0	/* no error */
#define	 NT_SMC_SWITCH_ERR	1	/* error, return to NT */

#define NT_SMC_FIFO_WRITE 3	/* write to fifo */
#define NT_SMC_FIFO_READ  4	/* read from fifo */
#define NT_SMC_FIFO_REG_WRITE 5	/* fifo register write */
#define NT_SMC_FIFO_REG_READ 6	/* fifo register read */
#define NT_SMC_STEAL_GET 0x10   /* get stolen time */


#ifndef __ASSEMBLY__

struct csrvisor_fifo_msg;
struct csrvisor_fifo_io_req;

#ifdef CONFIG_SECURITY_MODE

struct csrvisor_smc_args {
	void *arg0;
	void *arg1;
	void *arg2;
	void *arg3;
	void *arg4;
};

/* syscall interfaces to secure OS */
static inline void csrvisor_switch_to_nt(volatile struct csrvisor_smc_args
	*ret_args)
{
	ret_args->arg0 = (void *)T_SMC_SWITCH_OK;
	while (1) {
		__asm__ __volatile__(".arch_extension sec\n\t"
		"mov r0, %0\n\t" "mov r1, %1\n\t"
		"smc 0\n\t" :	/* no output */
		: "I"(T_SMC_SWITCH), "r"(ret_args)
		: "r0", "r1", "memory");
		if (ret_args->arg0 != (void *)T_SMC_SWITCH_OK)
			return;
	}
}

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

static inline void csrvisor_reboot_nt(void)
{
	__asm__ __volatile__(".arch_extension sec\n\t"
	"mov r0, %0\n\t"
	"smc 0\n\t" :	/* no output */
	: "I"(T_SMC_REBOOT)
	: "r0", "memory");
}

static inline void csrvisor_prof_init(void)
{
	__asm__ __volatile__(".arch_extension sec\n\t"
	"mov r0, %0\n\t"
	"smc 0\n\t" :	/* no output */
	: "I"(T_SMC_PROF_INIT)
	: "r0", "memory");
}

static inline void
csrvisor_prof_get(uint32_t *fiq, uint32_t *irq,
		  uint32_t *nt2t, uint32_t *t2nt)
{
	struct csrvisor_smc_args args;

	args.arg0 = fiq;
	args.arg1 = irq;
	args.arg2 = nt2t;
	args.arg3 = t2nt;

	__asm__ __volatile__(".arch_extension sec\n\t"
	"mov r0, %0\n\t" "mov r1, %1\n\t"
	"smc 0\n\t" :	/* no output */
	: "I"(T_SMC_PROF_GET), "r"(&args)
	: "r0", "r1", "memory");
}

#else
/* syscall interfaces to non-secure OS */
static inline void csrvisor_switch_to_t(int arg0, int arg1)
{
	__asm__ __volatile__(".arch_extension sec\n\t"
	"mov r0, %0\n\t" "mov r1, %1\n\t"
	"mov r2, %2\n\t" "smc 0\n\t" :	/* no output */
	: "I"(NT_SMC_SWITCH), "r"(arg0), "r"(arg1)
	: "r0", "r1", "r2", "memory");
}


#define CP15_DCACHE_INVALIDATE_CLEAN() \
	__asm__ __volatile__ ("mcr p15, 0, %0, c7, c14, 0" : : "r"(0))

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
