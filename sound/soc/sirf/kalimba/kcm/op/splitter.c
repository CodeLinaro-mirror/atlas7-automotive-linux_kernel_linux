#include <linux/module.h>
#include "../kasobj.h"
#include "../kasop.h"
#include "../kcm.h"
#include "../../dsp.h"

/* Splitter is simple enough, framework has done all the job */
static struct kasop_impl splitter_impl = {
};

static int __init kasop_init_splitter(void)
{
	return kcm_register_cap(CAPABILITY_ID_SPLITTER, &splitter_impl);
}

subsys_initcall(kasop_init_splitter);
