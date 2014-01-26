#ifndef __SIRFSOC_DECODER_OP_H_
#define __SIRFSOC_DECODER_OP_H_
enum input_t {
	INPUT_YC = 0,
	INPUT_CVBS_AIN1,
	INPUT_CVBS_AIN2,
	INPUT_COLORBAR,
	INPUT_ANY,
};

struct sirfsoc_decoder_ops {
	char desc[16];
	int (*detect)(void);
	int (*init)(void);
	int (*deinit)(void);
	int (*start)(int);
	int (*stop)(void);
	int (*set_fmt)(void);
	struct list_head list;
};

int sirfsoc_register_decoder_ops(struct sirfsoc_decoder_ops *ops);
#endif
