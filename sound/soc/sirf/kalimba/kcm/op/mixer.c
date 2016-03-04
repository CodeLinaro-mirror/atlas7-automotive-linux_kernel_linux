#include <linux/module.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "../kasobj.h"
#include "../kasop.h"
#include "../kcm.h"
#include "../../dsp.h"
#include "utils.h"

#define MAX_STREAMS	3

#define MIN_DB		-120
#define STEP_DB		1
#define MAXV		(-MIN_DB / STEP_DB)
#define DEFV		(MAXV - 7)	/* Big noise if all streams are 0dB */

static const DECLARE_TLV_DB_SCALE(vol_tlv, -120*100, STEP_DB*100, 0);

struct mixer_ctx {
	int gain[MAX_STREAMS];
	int muted[MAX_STREAMS];
	int streams;
	int ramp;		/* TODO: ready in Kalimba? */
	u16 primary_stream;	/* Starts from 1 */
};

static void set_gain(struct kasobj_op *op)
{
	int i;
	struct mixer_ctx *ctx = op->context;
	short db[MAX_STREAMS];

	if (!op->obj.life_cnt)
		return;

	for (i = 0; i < ctx->streams; i++) {
		if (ctx->muted[i])
			db[i] = -32768;
		else
			db[i] = (ctx->gain[i] - MAXV) * 60;
	}
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_GAINS,
			ctx->streams, db, NULL, NULL, __kcm_resp);
}

static void set_primary_stream(struct kasobj_op *op)
{
	struct mixer_ctx *ctx = op->context;

	if (op->obj.life_cnt)
		kalimba_operator_message(op->op_id,
				OPERATOR_MSG_SET_PRIMARY_STREAM, 1,
				&ctx->primary_stream, NULL, NULL, __kcm_resp);
}

/* Find primary stream (last active stream) */
static int select_primary_stream(struct kasobj_op *op)
{
	int i;
	struct mixer_ctx *ctx = op->context;

	/* Check pin (0,4,8) if 3x4 or (0,6) if 2x6 */
	for (i = ctx->streams - 1; i >= 0; i--)
		if (op->active_sink_pins & BIT(i * 12 / ctx->streams))
			return i + 1;

	return ctx->primary_stream;
}

/* Endpoint activity changed, we may have to pick new primary stream */
static void pin_changed(struct kasobj_op *op, int is_sink)
{
	if (is_sink) {
		/* Pick primary stream based on current input pins activity */
		int primary_stream = select_primary_stream(op);
		struct mixer_ctx *ctx = op->context;

		if (primary_stream != ctx->primary_stream) {
			ctx->primary_stream = primary_stream;
			set_primary_stream(op);
			kcm_debug("KASOP(%s): set primary stream to %d\n",
					op->obj.name, primary_stream);
		}
	}
}

static int vol_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int stream_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &stream_idx);
	struct mixer_ctx *ctx = op->context;

	BUG_ON(stream_idx < 0 || stream_idx >= ctx->streams);
	ucontrol->value.integer.value[0] = ctx->gain[stream_idx];
	return 0;
}

static int vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int stream_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &stream_idx);
	struct mixer_ctx *ctx = op->context;
	int gain = ucontrol->value.integer.value[0];

	BUG_ON(stream_idx < 0 || stream_idx >= ctx->streams);
	if (gain == ctx->gain[stream_idx] && !ctx->muted[stream_idx])
		return 0;

	kcm_lock();
	if (gain != ctx->gain[stream_idx] || ctx->muted[stream_idx]) {
		ctx->muted[stream_idx] = 0;
		ctx->gain[stream_idx] = ucontrol->value.integer.value[0];
		set_gain(op);
	}
	kcm_unlock();

	return 0;
}

static int mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int stream_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &stream_idx);
	struct mixer_ctx *ctx = op->context;

	BUG_ON(stream_idx < 0 || stream_idx >= ctx->streams);
	ucontrol->value.integer.value[0] = ctx->muted[stream_idx];
	return 0;
}

static int mute_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int stream_idx;
	struct kasobj_op *op = kasobj_ctrl_get_op(kcontrol, &stream_idx);
	struct mixer_ctx *ctx = op->context;
	int tomute = ucontrol->value.integer.value[0];

	BUG_ON(stream_idx < 0 || stream_idx >= ctx->streams);
	if (tomute == ctx->muted[stream_idx])
		return 0;

	kcm_lock();
	if (tomute != ctx->muted[stream_idx]) {
		ctx->muted[stream_idx] = tomute;
		set_gain(op);
	}
	kcm_unlock();

	return 0;
}

static int mixer_init(struct kasobj_op *op)
{
	int i, volume_idx = 0, mute_idx = 0;
	char names_buf[256], *names = names_buf, *name;
	struct mixer_ctx *ctx = kzalloc(sizeof(struct mixer_ctx), GFP_KERNEL);
	struct snd_kcontrol_new *ctrl;

	op->context = ctx;

	ctx->streams = op->db->param.mixer_streams;
	if (ctx->streams > MAX_STREAMS) {
		pr_err("KASOBJ(%s): stream count > %d!\n", op->obj.name,
			MAX_STREAMS);
		ctx->streams = MAX_STREAMS;
	}
	for (i = 0; i < MAX_STREAMS; i++)
		ctx->gain[i] = DEFV;

	if (op->db->rate == 0) {
		pr_err("KASOP(%s): invalid sample rate!\n", op->obj.name);
		return -EINVAL;
	}

	if (!op->db->ctrl_names.s)
		return 0;

	if (snprintf(names_buf, 256, "%s", op->db->ctrl_names.s) >= 256) {
		pr_err("KASOP(%s): control names too long!\n", op->obj.name);
		return -EINVAL;
	}

	while ((name = strsep(&names, ":;"))) {
		if (kcm_strcasestr(name, "Volume")) {
			if (volume_idx >= ctx->streams) {
				pr_err("KASOP(%s): too many Volume controls!\n",
					       op->obj.name);
				continue;
			}
			ctrl = kasop_ctrl_single_ext_tlv(name, op, MAXV,
					vol_get, vol_put, vol_tlv, volume_idx);
			kcm_register_ctrl(ctrl);
			volume_idx++;
		} else if (kcm_strcasestr(name, "Mute")) {
			if (mute_idx >= ctx->streams) {
				pr_err("KASOP(%s): too many Mute controls!\n",
					       op->obj.name);
				continue;
			}
			ctrl = kasop_ctrl_single_ext_tlv(name, op, 1,
					mute_get, mute_put, NULL, mute_idx);
			kcm_register_ctrl(ctrl);
			mute_idx++;
		} else {
			pr_err("KASOP(%s): unknown control '%s'!\n",
					op->obj.name, name);
		}
	}

	return 0;
}

static int mixer_create(struct kasobj_op *op, const struct kasobj_param *param)
{
	struct mixer_ctx *ctx = op->context;
	static short stream3x4_cfg[] = { 4, 4, 4 };
	static short stream2x6_cfg[] = { 6, 6 };
	short *stream_cfg;
	short rate = op->db->rate;

	if (ctx->streams == 2) {
		stream_cfg = stream2x6_cfg;
	} else {
		stream_cfg = stream3x4_cfg;
		if (ctx->streams != 3)
			pr_err("KASOBJ(%s): unsupported streams %d!\n",
					op->obj.name, ctx->streams);
	}
	kalimba_operator_message(op->op_id, OPERATOR_MSG_SET_CHANNELS,
			ctx->streams, stream_cfg, NULL, NULL, __kcm_resp);

	kalimba_operator_message(op->op_id, OPMSG_COMMON_SET_SAMPLE_RATE,
			1, &rate, NULL, NULL, __kcm_resp);

	set_gain(op);

	ctx->primary_stream = 1;
	set_primary_stream(op);

	return 0;
}

static int mixer_trigger(struct kasobj_op *op, int event)
{
	const int param = KASOP_GET_PARAM(event);

	switch (KASOP_GET_EVENT(event)) {
	case kasop_event_start_ep:
	case kasop_event_stop_ep:
		pin_changed(op, param);
		break;
	default:
		break;
	}

	return 0;
}

static struct kasop_impl mixer_impl = {
	.init = mixer_init,
	.create = mixer_create,
	.trigger = mixer_trigger,
};

static int __init kasop_init_mixer(void)
{
	return kcm_register_cap(CAPABILITY_ID_MIXER, &mixer_impl);
}

subsys_initcall(kasop_init_mixer);
