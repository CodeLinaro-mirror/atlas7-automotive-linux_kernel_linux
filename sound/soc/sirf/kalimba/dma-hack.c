/*
 * Temporary workaround to initialize DMAC by ARM.
 * TODO: Remove this file after Kalimba takes over the job.
 *
 * No locking is required as only channel specific registers are written,
 * interrupt and loop ctrl have separated set/clr registers.
 *
 * XXX: DMA driver read INT_EN and LOOP_CTRL before write (sirf-dma.c:222,241).
 *      It might re-enable usp3 interrupt after kalimba disables it.
 */
#include <linux/io.h>
#include <linux/printk.h>

/* Base */
#define DMAC3_BASE		0x10D60000
#define DMAC4_BASE		0x11002000

/* Offset */
#define DMA_CH_ADDR(n)		((n) << 4)
#define DMA_CH_XLEN(n)		(DMA_CH_ADDR(n) + 0x04)
#define DMA_CH_YLEN(n)		(DMA_CH_ADDR(n) + 0x08)
#define DMA_CH_CTRL(n)		(DMA_CH_ADDR(n) + 0x0C)
#define DMA_CH_WIDTH(n)		(0x0100 + ((n) << 2))
#define DMA_CH_VALID		0x0140
#define DMA_CH_INT		0x0144
#define DMA_INT_EN_SET		0x0148
#define DMA_INT_EN_CLR		0x014C
#define DMA_CH_LOOP_CTRL_SET	0x0158
#define DMA_CH_LOOP_CTRL_CLR	0x015C

/* Initialize DMA channel to loop mode
 * - dmac, ch: DMA controller number, channel number
 * - addr, buf_len: DMA buffer address and size
 * - dir: 1 - mem->dev, 0 - dev->mem
 * - burst: 1 - burst mode, 0 - single mode
 */
void __dmac_enable(int dmac, int ch, u32 addr, int buf_len, int dir, int burst)
{
	void __iomem *base;
	int i, ylen, width = 0, period_len = buf_len / 2;

	/* Calculate YLEN and DMA_WIDTH */
	for (i = 1024; i >= 16; i /= 2) {
		if (!(period_len % i)) {
			width = i / 4;
			break;
		}
	}
	if (width == 0) {
		pr_err("Invalid buffer size\n");
		return;
	}
	ylen = buf_len / (width * 4) - 1;

	if (dmac == 3) {
		base = ioremap_nocache(DMAC3_BASE, 0x1000);
	} else if (dmac == 4) {
		base = ioremap_nocache(DMAC4_BASE, 0x1000);
	} else {
		pr_err("Invalid DMA controller %d\n", dmac);
		return;
	}

	dir = !!dir;
	burst = !!burst;

	/* Clear pending interrupt */
	writel(BIT(ch), base + DMA_CH_INT);

	/* Configure DMA channel */
	writel_relaxed(width, base + DMA_CH_WIDTH(ch));
	writel_relaxed(ch | (burst << 4) | (dir << 5), base + DMA_CH_CTRL(ch));
	writel_relaxed(0, base + DMA_CH_XLEN(ch));
	writel_relaxed(ylen, base + DMA_CH_YLEN(ch));

	/* Start DMA */
	writel_relaxed(BIT(ch), base + DMA_INT_EN_SET);
	writel_relaxed(BIT(ch) | BIT(ch+16), base + DMA_CH_LOOP_CTRL_SET);
	writel(addr >> 2, base + DMA_CH_ADDR(ch));

	iounmap(base);
}

void __dmac_disable(int dmac, int ch)
{
	void __iomem *base;

	if (dmac == 3) {
		base = ioremap_nocache(DMAC3_BASE, 0x1000);
	} else if (dmac == 4) {
		base = ioremap_nocache(DMAC4_BASE, 0x1000);
	} else {
		pr_err("Invalid DMA controller %d\n", dmac);
		return;
	}

	/* Disable DMA channel */
	writel_relaxed(BIT(ch), base + DMA_INT_EN_CLR);
	writel_relaxed(BIT(ch) | BIT(ch+16), base + DMA_CH_LOOP_CTRL_CLR);

	iounmap(base);
}
