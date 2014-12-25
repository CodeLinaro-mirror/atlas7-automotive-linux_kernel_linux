#ifndef LINUX_NANDDISK_IOCTL_H
#define LINUX_NANDDISK_IOCTL_H

struct nanddisk_ioctl {
	unsigned int handle;
	unsigned int op;
	void     *in_buf;
	unsigned in_buf_size;
	void     *out_buf;
	unsigned out_buf_size;
};

#define NANDDISK_IOC_MAGIC 'n'
#define NANDDISK_IOCTL _IOWR(NANDDISK_IOC_MAGIC, 0x20, struct nanddisk_ioctl)

#endif /* LINUX_NANDDISK_IOCTL_H */
