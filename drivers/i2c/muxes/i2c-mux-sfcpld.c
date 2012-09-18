#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <asm/delay.h>

struct sirffpga_cpld {
	struct i2c_client	*client;
};

static const struct i2c_device_id sfcpld_id[] = {
	{ "fpga-cpld", NULL },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sfcpld_id);

static int sfcpld_init_set_default_status(struct i2c_client *client)
{
	struct i2c_msg msg[2];
	char cs_clear[2] = {0xf, 0x0};
	char cs_dm9k[2] = {0xf, 0x2};
	char dm9k_rst[2] = {0x8, 0x0};
	char id[2] = {0x0, 0x0};

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
	i2c_transfer(client->adapter, msg, 2);

	dev_info(&client->dev, "cpld version:0x%02x%02x found\n", id[0], id[1]);

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
	return i2c_master_send(client, cs_dm9k, 2);
}

static int __devinit sfcpld_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct sirffpga_cpld	*sfcpld;
	int			err = -ENODEV;
	int			tmp;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	sfcpld = kzalloc(sizeof(*sfcpld), GFP_KERNEL);
	if (!sfcpld)
		return -ENOMEM;

	i2c_set_clientdata(client, sfcpld);
	sfcpld->client	= client;

	if (sfcpld_init_set_default_status(client) < 0)
		goto exit_free;

	return 0;

exit_free:
	kfree(sfcpld);
	return err;
}

static int __devexit sfcpld_remove(struct i2c_client *client)
{
	struct sfcpld *sfcpld = i2c_get_clientdata(client);
	kfree(sfcpld);
	return 0;
}

static struct i2c_driver sfcpld_driver = {
	.driver = {
		.name	= "sirf-fpgacpld",
		.owner	= THIS_MODULE,
	},
	.probe		= sfcpld_probe,
	.remove		= __devexit_p(sfcpld_remove),
	.id_table	= sfcpld_id,
};

module_i2c_driver(sfcpld_driver);

MODULE_DESCRIPTION("SiRF FPGA on-board CPLD driver");
MODULE_LICENSE("GPL");
