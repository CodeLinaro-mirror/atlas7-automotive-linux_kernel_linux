#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <asm/delay.h>
#include <asm/io.h>

struct sirffpga_cpld {
	struct i2c_client	*client;
	struct task_struct	*irq_tsk;
};

static const struct i2c_device_id sfcpld_id[] = {
	{ "fpga-cpld", NULL },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sfcpld_id);

static u32 sfcpld_irq_status;
struct sirffpga_cpld *sffc;

static int sfcpld_init_set_default_status(struct i2c_client *client)
{
	struct i2c_msg msg[2];
	char id[2] = {0x0, 0x0};
	int ret = 0;

	/* read CPLD ID */

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 1;
	msg[0].buf = id;
	msg[1].addr = client->addr;
	msg[1].flags = client->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = id;
	ret = i2c_transfer(client->adapter, msg, 2);

	if (ret < 0)
		goto out;

	dev_info(&client->dev, "cpld version:0x%02x%02x found\n", id[0], id[1]);

#if defined(SIRF_OLD_FPGA)
	{
		char cs_clear[2] = {0xf, 0x0};
		char cs_dm9k[2] = {0xf, 0x2};
		char dm9k_rst[2] = {0x8, 0x0};

		/* reset DM9000 */

		msg[0].addr = client->addr;
		msg[0].flags = client->flags & I2C_M_TEN;
		msg[0].len = 1;
		msg[0].buf = dm9k_rst;
		msg[1].addr = client->addr;
		msg[1].flags = client->flags & I2C_M_TEN;
		msg[1].flags |= I2C_M_RD;
		msg[1].len = 1;
		msg[1].buf = dm9k_rst + 1;
		i2c_transfer(client->adapter, msg, 2);

		dm9k_rst[1] &= ~0x80; /* 7: enet_rst */
		i2c_master_send(client, dm9k_rst, 2);
		udelay(50);
		dm9k_rst[1] |= 0x80;
		i2c_master_send(client, dm9k_rst, 2);

		/* let VIP pin switch to DM9000*/

		msg[0].addr = client->addr;
		msg[0].flags = client->flags & I2C_M_TEN;
		msg[0].len = 1;
		msg[0].buf = cs_clear;
		msg[1].addr = client->addr;
		msg[1].flags = client->flags & I2C_M_TEN;
		msg[1].flags |= I2C_M_RD;
		msg[1].len = 1;
		msg[1].buf = cs_clear + 1;
		i2c_transfer(client->adapter, msg, 2);

		/* [1:0] VIP pin switch
		 * 00 VIP = CAM
		 * 01 VIP = TV
		 * 10 VIP = ENET
		 * 11 NULL"
		 */
		cs_clear[1] &= ~0x3;
		i2c_master_send(client, cs_clear, 2);

		cs_dm9k[1] = cs_clear[1] | 0x2;
		i2c_master_send(client, cs_dm9k, 2);
	}
#else
	{
		char irq_enable[2] = {0xe, 0x0};

		/* enable eint irq pin */

		msg[0].addr = client->addr;
		msg[0].flags = client->flags & I2C_M_TEN;
		msg[0].len = 1;
		msg[0].buf = irq_enable;
		msg[1].addr = client->addr;
		msg[1].flags = client->flags & I2C_M_TEN;
		msg[1].flags |= I2C_M_RD;
		msg[1].len = 1;
		msg[1].buf = irq_enable + 1;
		i2c_transfer(client->adapter, msg, 1);

		irq_enable[1] |= 0x1;
		i2c_master_send(client, irq_enable, 2);
	}
#endif

out:
	return ret;
}

/*
 * this thread will run at CPU0. it depends on I2C0 irq who will happen at CPU1
 * its users' irq handler will run in CPU1
 */

static int sfcpld_irq_get_stat_thread(void *data)
{
	struct i2c_msg msg[2];
	struct i2c_client *client = data;

	u8 irq[2] = {0x4, 0x0};

	sched_setaffinity(current->pid, cpumask_of(0));
	set_user_nice(current, -20);

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		schedule();

		/* read IRQ stat */
		msg[0].addr = client->addr;
		msg[0].flags = client->flags & I2C_M_TEN;
		msg[0].len = 1;
		msg[0].buf = irq;
		msg[1].addr = client->addr;
		msg[1].flags = client->flags & I2C_M_TEN;
		msg[1].flags |= I2C_M_RD;
		msg[1].len = 1;
		msg[1].buf = irq + 1;
		i2c_transfer(client->adapter, msg, 1);

		sfcpld_irq_status |= BIT(31) | irq[1];
	}

	return 0;
}

/*
 * CPLD's IRQ users will call this functions at IRQ handlers from CPU1
 */
u32 sfcpld_irq_get_status(void)
{
	if (!smp_processor_id()) {
		WARN_ON(1);
		return -EINVAL;
	}

	sfcpld_irq_status = 0;

	wake_up_process(sffc->irq_tsk);

	do {
		cpu_relax();
	} while (!(sfcpld_irq_status & BIT(31)));

	return sfcpld_irq_status & 0xFF;
}
EXPORT_SYMBOL(sfcpld_irq_get_status);

static int __devinit sfcpld_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct sirffpga_cpld	*sfcpld;
	int			err = -ENODEV;
	int			tmp;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);

	sffc = sfcpld = kzalloc(sizeof(*sfcpld), GFP_KERNEL);
	if (!sfcpld)
		return -ENOMEM;

	i2c_set_clientdata(client, sfcpld);
	sfcpld->client = client;

	if (sfcpld_init_set_default_status(client) < 0)
		goto exit_free;

#if defined(SIRF_OLD_FPGA)
	do {
		/*
		 * init ROM interface, all these codes are temp for FPGA
		 * they are in u-boot before
		 */
#define SIRFSOC_ROMIF_BASE 0xcc000000
#define ROM_CFG_CS1 0x4
		void *rom_base = ioremap(SIRFSOC_ROMIF_BASE, 0x100);
		if (!rom_base)
			goto exit_free;
		*(u32 *)(rom_base + ROM_CFG_CS1) = 0xe59ff018;
		iounmap(rom_base);
	} while (0);
#else
#define SIRF_I2C0_IRQ 56
#define SIRF_GPIO0_IRQ 75
	/*
	 * make all devices whose irq is connected with CPLD
	 * interrupt at CPU1, and I2C thread will work at CPU0
	 */
	irq_set_affinity(SIRF_I2C0_IRQ, cpumask_of(0));
	irq_set_affinity(SIRF_GPIO0_IRQ, cpumask_of(1));
#endif

	sfcpld->irq_tsk = kthread_create(sfcpld_irq_get_stat_thread, client,
		"sfcpld-irq");

	if (IS_ERR(sfcpld->irq_tsk)) {
		dev_err(&client->dev, "disabled - Unable to start kernel thread\n");
		goto exit_free;
	}

	wake_up_process(sfcpld->irq_tsk);

	return 0;

exit_free:
	dev_err(&client->dev, "fails to init\n");
	kfree(sfcpld);
	return err;
}

static struct i2c_driver sfcpld_driver = {
	.driver = {
		.name	= "sirf-fpgacpld",
		.owner	= THIS_MODULE,
	},
	.probe		= sfcpld_probe,
	.id_table	= sfcpld_id,
};
module_i2c_driver(sfcpld_driver);

MODULE_DESCRIPTION("SiRF FPGA on-board CPLD driver");
MODULE_LICENSE("GPL");
