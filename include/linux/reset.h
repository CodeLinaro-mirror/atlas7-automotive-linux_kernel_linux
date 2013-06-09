#ifndef _LINUX_RESET_H_
#define _LINUX_RESET_H_

struct device;
struct reset_control;

#ifdef CONFIG_RESET_CONTROLLER
int reset_control_reset(struct reset_control *rstc);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);

struct reset_control *reset_control_get(struct device *dev, const char *id);
void reset_control_put(struct reset_control *rstc);
struct reset_control *devm_reset_control_get(struct device *dev, const char *id);

int device_reset(struct device *dev);

#else /* ! CONFIG_RESET_CONTROLLER */

static inline int reset_control_reset(struct reset_control *rstc)
{
	return -ENOSYS;
}

static inline int reset_control_assert(struct reset_control *rstc);
{
	return -ENOSYS;
}

static inline int reset_control_deassert(struct reset_control *rstc)
{
	return -ENOSYS;
}

static inline struct reset_control *reset_control_get(struct device *dev, const char *id)
{
	return -ENOSYS;
}

static inline void reset_control_put(struct reset_control *rstc)
{
	/* reset_control_get never succeed */
	WARN_ON(1);
}

static inline struct reset_control *devm_reset_control_get(struct device *dev, const char *id);
{
	return -ENOSYS;
}

static inline int device_reset(struct device *dev)
{
	/* device_reset will never really be done */
	WARN_ON(1);

	return -ENOSYS;
}

#endif

#endif
