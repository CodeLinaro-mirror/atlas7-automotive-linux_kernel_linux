#ifndef _FIRMWARE_H
#define _FIRMWARE_H

int firmware_ioctl(struct regmap *regmap, struct device *dev,
		unsigned int cmd, unsigned long arg);
void firmware_download(struct regmap *regmap, u32 *fw_data);
#endif /* _FIRMWARE_H */
