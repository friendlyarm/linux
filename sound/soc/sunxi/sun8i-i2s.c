/*
 * Allwinner sun8i I2S sound card
 *
 * Copyright (C) 2016 Jean-Francois Moine <moinejf at free.fr>
 * Copyright (C) 2017 Anthony Lee <don.anthony.lee at gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>

#if (0)
#define DBGOUT(msg...)		do { printk(KERN_ERR msg); } while (0)
#else
#define DBGOUT(msg...)		do {} while (0)
#endif

/* --- hardware --- */

#define I2S_CTL 	  	0x00
	/* common */
	#define I2S_CTL_SDOEN_MSK	(0x0f00)
	#define I2S_CTL_SDO3EN		BIT(11)
	#define I2S_CTL_SDO2EN		BIT(10)
	#define I2S_CTL_SDO1EN		BIT(9)
	#define I2S_CTL_SDO0EN		BIT(8)
	#define I2S_CTL_OUTMUTE		BIT(6)
	#define I2S_CTL_TXEN		BIT(2)
	#define I2S_CTL_RXEN		BIT(1)
	#define I2S_CTL_GEN		BIT(0)
	/* a83t */
	#define I2S_CTL_A83T_MS		BIT(5)
	#define I2S_CTL_A83T_PCM	BIT(4)
	/* h3 */
	#define I2S_CTL_H3_BCLKOUT	BIT(18)
	#define I2S_CTL_H3_LRCKOUT	BIT(17)
	#define I2S_CTL_H3_MODE_MSK	(3 << 4)
	#define I2S_CTL_H3_MODE_I2S	(1 << 4)
	#define I2S_CTL_H3_MODE_RGT	(2 << 4)

#define I2S_FAT0 		0x04
	/* common */
	/* a83t */
	#define I2S_FAT0_A83T_LRCP		BIT(7)
	#define I2S_FAT0_A83T_BCP		BIT(6)
	#define I2S_FAT0_A83T_SR_16BIT		(0 << 4)
	#define I2S_FAT0_A83T_SR_24BIT		(2 << 4)
	#define I2S_FAT0_A83T_SR_MSK		(3 << 4)
	#define I2S_FAT0_A83T_WSS_32BCLK	(3 << 2)
	#define I2S_FAT0_A83T_FMT_I2S1		(0 << 0)
	#define I2S_FAT0_A83T_FMT_LFT		(1 << 0)
	#define I2S_FAT0_A83T_FMT_RGT		(2 << 0)
	#define I2S_FAT0_A83T_FMT_MSK		(3 << 0)
	/* h3 */
	#define I2S_FAT0_H3_LRCKR_PERIOD(v) ((v) << 20)
	#define I2S_FAT0_H3_LRCKR_PERIOD_MSK (0x3ff << 20)
	#define I2S_FAT0_H3_LRCK_POLARITY	BIT(19)
	#define I2S_FAT0_H3_LRCK_PERIOD(v)	((v) << 8)
	#define I2S_FAT0_H3_LRCK_PERIOD_MSK (0x3ff << 8)
	#define I2S_FAT0_H3_BCLK_POLARITY	BIT(7)
	#define I2S_FAT0_H3_SR_16		(3 << 4)
	#define I2S_FAT0_H3_SR_24		(5 << 4)
	#define I2S_FAT0_H3_SR_MSK		(7 << 4)
	#define I2S_FAT0_H3_SW_16		(3 << 0)
	#define I2S_FAT0_H3_SW_32		(7 << 0)
	#define I2S_FAT0_H3_SW_MSK		(7 << 0)

#define I2S_FAT1		0x08

#define I2S_FCTL		0x14
	#define I2S_FCTL_FTX		BIT(25)
	#define I2S_FCTL_FRX		BIT(24)
	#define I2S_FCTL_TXTL(v)	((v) << 12)
	#define I2S_FCTL_TXIM		BIT(2)

#define I2S_INT    		0x1c
	#define I2S_INT_TXDRQEN		BIT(7)

#define I2S_RXFIFO		0x10
#define I2S_TXFIFO		0x20
#define I2S_FSTA		0x18
	
#define I2S_CLKD   		0x24
	/* common */
	#define I2S_CLKD_BCLKDIV(v)	((v) << 4)
	#define I2S_CLKD_MCLKDIV(v)	((v) << 0)
	/* a83t */
	#define I2S_CLKD_A83T_MCLKOEN	BIT(7)
	/* h3 */
	#define I2S_CLKD_H3_MCLKOEN	BIT(8)

#define I2S_TXCNT  		0x28

#define I2S_RXCNT  		0x2c

/* --- A83T --- */
#define I2S_TXCHSEL_A83T	0x30
	#define I2S_TXCHSEL_A83T_CHNUM(v)	(((v) - 1) << 0)
	#define I2S_TXCHSEL_A83T_CHNUM_MSK	(7 << 0)

#define I2S_TXCHMAP_A83T	0x34

/* --- H3 --- */
#define I2S_TXCHCFG_H3		0x30
	#define I2S_TXCHCFG_H3_TX_SLOT_NUM_MSK (7 << 0)
	#define I2S_TXCHCFG_H3_TX_SLOT_NUM(v) ((v) << 0)

#define I2S_TX0CHSEL_H3		0x34		/* 0..3 */
	#define I2S_TXn_H3_OFFSET_MSK	(3 << 12)
	#define I2S_TXn_H3_OFFSET(v)	((v) << 12)
	#define I2S_TXn_H3_CHEN_MSK	(0xff << 4)
	#define I2S_TXn_H3_CHEN(v)	((v) << 4)
	#define I2S_TXn_H3_CHSEL_MSK	(7 << 0)
	#define I2S_TXn_H3_CHSEL(v)	((v) << 0)

#define I2S_TX0CHMAP_H3		0x44		/* 0..3 */

#define I2S_RXCHSEL_H3		0x54
#define I2S_RXCHMAP_H3		0x58

/* --- driver --- */

#define I2S_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S32_LE)

#define PCM_LRCK_PERIOD 32
#define PCM_LRCKR_PERIOD 1

#define SOC_A83T 0
#define SOC_H3 1
#define SOC_H5 2

struct priv {
	struct clk *mod_clk;
	struct clk *bus_clk;
	struct regmap *regmap;
	struct reset_control *rstc;

	int type;
	int nchan;

	struct snd_dmaengine_dai_dma_data playback_dma_data;
};

static const struct of_device_id sun8i_i2s_of_match[] = {
	{ .compatible = "allwinner,sun8i-a83t-i2s",
				.data = (void *) SOC_A83T },
	{ .compatible = "allwinner,sun8i-h3-i2s",
				.data = (void *) SOC_H3 },
	{ .compatible = "allwinner,sun8i-h5-i2s",
				.data = (void *) SOC_H5 },
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_i2s_of_match);

/* --- CPU DAI --- */

static void sun8i_i2s_init(struct priv *priv)
{
	/* disable global */
	regmap_update_bits(priv->regmap, I2S_CTL,
			   I2S_CTL_GEN | I2S_CTL_RXEN | I2S_CTL_TXEN,
			   0);

	priv->nchan = 2;

	/* A83T */
	if (priv->type == SOC_A83T) {
		regmap_update_bits(priv->regmap, I2S_CTL,
				   I2S_CTL_A83T_MS |		/* codec clk & FRM slave */
					I2S_CTL_A83T_PCM,	/* I2S mode */
				   0);

		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_A83T_FMT_MSK,
				   0);
		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_A83T_FMT_I2S1,
				   I2S_FAT0_A83T_FMT_I2S1);
		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_A83T_LRCP | I2S_FAT0_A83T_BCP,
				   0);

		regmap_write(priv->regmap, I2S_FCTL,
			     I2S_FCTL_TXIM | /* fifo */
				I2S_FCTL_TXTL(0x40));

		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_A83T_LRCP |		/* normal bit clock + frame */
					I2S_FAT0_A83T_BCP,
				   0);
	/* H3 */
	} else {
		regmap_update_bits(priv->regmap, I2S_FCTL,
				   I2S_FCTL_FRX | I2S_FCTL_FTX,	/* clear the FIFOs */
				   0);

		regmap_write(priv->regmap, I2S_TXCNT, 0); /* FIFO counters */
		regmap_write(priv->regmap, I2S_RXCNT, 0);

		regmap_update_bits(priv->regmap, I2S_CTL,
				   I2S_CTL_H3_LRCKOUT | I2S_CTL_H3_BCLKOUT,
				   I2S_CTL_H3_LRCKOUT | I2S_CTL_H3_BCLKOUT);

		regmap_update_bits(priv->regmap, I2S_CTL,
				   I2S_CTL_H3_MODE_MSK,
				   I2S_CTL_H3_MODE_I2S);

		regmap_update_bits(priv->regmap, I2S_TX0CHSEL_H3,
				   I2S_TXn_H3_OFFSET_MSK,
				   I2S_TXn_H3_OFFSET(1));

		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_H3_BCLK_POLARITY | I2S_FAT0_H3_LRCK_POLARITY, /* normal bclk & frame */
				   0);
	}
}

static int sun8i_i2s_set_clock(struct priv *priv, unsigned long rate)
{
	unsigned long freq;
	int ret, i, div;
	static const u8 div_tb[] = {
		1, 2, 4, 6, 8, 12, 16, 24,
	};

	DBGOUT("%s: rate = %lu.", __func__, rate);

	/* compute the sys clock rate and divide values */
	if (rate % 1000 == 0)
		freq = 24576000;
	else
		freq = 22579200;
	div = freq / 2 / PCM_LRCK_PERIOD / rate;
	if (priv->type == SOC_A83T)
		div /= 2;			/* bclk_div==0 => mclk/2 */
	for (i = 0; i < ARRAY_SIZE(div_tb) - 1; i++)
		if (div_tb[i] >= div)
			break;

	ret = clk_set_rate(priv->mod_clk, freq);
	if (ret) {
		pr_info("Setting sysclk rate failed %d\n", ret);
		return ret;
	}

	/* set the mclk and bclk dividor register */
	if (priv->type == SOC_A83T) {
		regmap_write(priv->regmap, I2S_CLKD,
			     I2S_CLKD_A83T_MCLKOEN | I2S_CLKD_MCLKDIV(i));
	} else {
		regmap_write(priv->regmap, I2S_CLKD,
			     I2S_CLKD_H3_MCLKOEN | I2S_CLKD_MCLKDIV(1) | I2S_CLKD_BCLKDIV(i + 1));
	}

	/* format */
	if (priv->type == SOC_A83T) {
		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_A83T_WSS_32BCLK | I2S_FAT0_A83T_SR_MSK,
				   I2S_FAT0_A83T_WSS_32BCLK | I2S_FAT0_A83T_SR_16BIT);
	} else {
		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_H3_LRCKR_PERIOD_MSK | I2S_FAT0_H3_LRCK_PERIOD_MSK,
				   I2S_FAT0_H3_LRCK_PERIOD(PCM_LRCK_PERIOD - 1) | I2S_FAT0_H3_LRCKR_PERIOD(PCM_LRCKR_PERIOD - 1));

		regmap_update_bits(priv->regmap, I2S_FAT0,
				   I2S_FAT0_H3_SW_MSK | I2S_FAT0_H3_SR_MSK,
				   I2S_FAT0_H3_SW_16 | I2S_FAT0_H3_SR_16);
	}
	regmap_write(priv->regmap, I2S_FAT1, 0);

	return 0;
}

static void sun8i_i2s_tx_set_channels(struct priv *priv, int nchan)
{
	u32 reg = 0;
	int n;

	DBGOUT("%s: nchan = %d", __func__, nchan);

	priv->nchan = nchan;
	if (priv->type == SOC_A83T) {
		regmap_update_bits(priv->regmap, I2S_TXCHSEL_A83T,
				   I2S_TXCHSEL_A83T_CHNUM_MSK,
				   I2S_TXCHSEL_A83T_CHNUM(nchan));

		switch (nchan) {
		case 1:
			regmap_write(priv->regmap, I2S_TXCHMAP_A83T,
				     0x76543200);
			break;
		case 8:
			regmap_write(priv->regmap, I2S_TXCHMAP_A83T,
				     0x54762310);
			break;
		default:
			/* left/right inversion of channels 0 and 1 */
			regmap_write(priv->regmap, I2S_TXCHMAP_A83T,
				     0x76543201);
			break;
		}
	} else {
		regmap_update_bits(priv->regmap, I2S_TXCHCFG_H3,
				   I2S_TXCHCFG_H3_TX_SLOT_NUM_MSK,
				   I2S_TXCHCFG_H3_TX_SLOT_NUM(nchan - 1));

		regmap_update_bits(priv->regmap, I2S_TX0CHSEL_H3,
				   I2S_TXn_H3_CHEN_MSK,
				   I2S_TXn_H3_CHEN((0x01 << nchan) - 1));
		regmap_update_bits(priv->regmap, I2S_TX0CHSEL_H3,
				   I2S_TXn_H3_CHSEL_MSK,
				   I2S_TXn_H3_CHSEL(nchan - 1));

		reg = 0;
		for(n = 0; n < nchan; n++) {
			reg |= (((0x00000001 << n) - 0x00000001) << (n * 4));
		}
		regmap_write(priv->regmap, I2S_TX0CHMAP_H3, reg);
	}

	reg = 0;
	if (nchan >= 7)
		reg |= I2S_CTL_SDO3EN;
	if (nchan >= 5)
		reg |= I2S_CTL_SDO2EN;
	if (nchan >= 3)
		reg |= I2S_CTL_SDO1EN;
	reg |= I2S_CTL_SDO0EN;
	regmap_update_bits(priv->regmap, I2S_CTL,
			   I2S_CTL_SDOEN_MSK,
			   reg);
}

static int sun8i_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);
	int nchan = priv->nchan;

	DBGOUT("%s: reached.", __func__);

	/* Enable the whole hardware block */
	regmap_update_bits(priv->regmap, I2S_CTL,
			   I2S_CTL_GEN,
			   I2S_CTL_GEN);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		sun8i_i2s_tx_set_channels(priv, nchan);
		regmap_write(priv->regmap, I2S_TXCNT, 0);
	} else { // TODO
		return -EINVAL;
	}

	return clk_prepare_enable(priv->mod_clk);
}

static void sun8i_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);

	DBGOUT("%s: reached.", __func__);

	clk_disable_unprepare(priv->mod_clk);

	regmap_update_bits(priv->regmap, I2S_CTL,
			   I2S_CTL_GEN,
			   0);
}

static int sun8i_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);
	int nchan = params_channels(params);
	int sample_resolution;
	int ret;

	DBGOUT("%s: reached line %d, rate = %u, format = %d, nchan = %d.",
	       __func__, __LINE__,
	       params_rate(params), params_format(params), nchan);

	if (nchan < 1 || nchan > 8) return -EINVAL;
	sun8i_i2s_tx_set_channels(priv, nchan);

	ret = sun8i_i2s_set_clock(priv, params_rate(params));
	if (ret)
		return ret;

	DBGOUT("%s: reached line %d.", __func__, __LINE__);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		priv->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		sample_resolution = 16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		priv->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		sample_resolution = 24;
		break;
	default:
		return -EINVAL;
	}

	DBGOUT("%s: sample_resolution = %d\n", __func__, sample_resolution);

	if (priv->type == SOC_A83T) {
		if (sample_resolution == 16) {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_A83T_SR_MSK,
					   I2S_FAT0_A83T_SR_16BIT);
			regmap_update_bits(priv->regmap, I2S_FCTL,
					   I2S_FCTL_TXIM,
					   I2S_FCTL_TXIM);
		} else {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_A83T_SR_MSK,
					   I2S_FAT0_A83T_SR_24BIT);
			regmap_update_bits(priv->regmap, I2S_FCTL,
					   I2S_FCTL_TXIM,
					   0);
		}
	} else {
		if (sample_resolution == 16) {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_H3_SR_MSK | I2S_FAT0_H3_SW_MSK,
					   I2S_FAT0_H3_SR_16 | I2S_FAT0_H3_SW_16);
			regmap_update_bits(priv->regmap, I2S_FCTL,
					   I2S_FCTL_TXIM,
					   I2S_FCTL_TXIM);
		} else {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_H3_SR_MSK | I2S_FAT0_H3_SW_MSK,
					   I2S_FAT0_H3_SR_24 | I2S_FAT0_H3_SW_32);
			regmap_update_bits(priv->regmap, I2S_FCTL,
					   I2S_FCTL_TXIM,
					   0);
		}
	}

	/* flush TX FIFO */
	regmap_update_bits(priv->regmap, I2S_FCTL,
			   I2S_FCTL_FTX,
			   I2S_FCTL_FTX);

	return 0;
}

static int sun8i_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);

	DBGOUT("%s: fmt = 0x%x.", __func__, fmt);

	/* DAI Mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_CTL,
					   I2S_CTL_H3_MODE_MSK,
					   I2S_CTL_H3_MODE_I2S);
		}
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_CTL,
					   I2S_CTL_H3_MODE_MSK,
					   I2S_CTL_H3_MODE_RGT);
		}
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_H3_BCLK_POLARITY | I2S_FAT0_H3_LRCK_POLARITY,
					   I2S_FAT0_H3_BCLK_POLARITY | I2S_FAT0_H3_LRCK_POLARITY);
		}
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_H3_BCLK_POLARITY | I2S_FAT0_H3_LRCK_POLARITY,
					   I2S_FAT0_H3_BCLK_POLARITY);
		}

		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_H3_BCLK_POLARITY | I2S_FAT0_H3_LRCK_POLARITY,
					   I2S_FAT0_H3_LRCK_POLARITY);
		}

		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_FAT0,
					   I2S_FAT0_H3_BCLK_POLARITY | I2S_FAT0_H3_LRCK_POLARITY,
					   0);
		}

		break;
	default:
		return -EINVAL;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Master */
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_CLKD,
					   I2S_CLKD_H3_MCLKOEN,
					   I2S_CLKD_H3_MCLKOEN);
		}
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Slave */
		if (priv->type == SOC_A83T) {
			// TODO
			return -EINVAL;
		} else {
			regmap_update_bits(priv->regmap, I2S_CLKD,
					   I2S_CLKD_H3_MCLKOEN,
					   0);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void sun8i_i2s_start_playback(struct priv *priv)
{
	/* Clear TX counter */
	regmap_write(priv->regmap, I2S_TXCNT, 0);

	/* Flush TX FIFO */
	regmap_update_bits(priv->regmap, I2S_FCTL,
			   I2S_FCTL_FTX,
			   I2S_FCTL_FTX);

	/* Enable TX Block */
	regmap_update_bits(priv->regmap, I2S_CTL,
			   I2S_CTL_TXEN,
			   I2S_CTL_TXEN);

	/* Enable TX DRQ */
	regmap_update_bits(priv->regmap, I2S_INT,
			   I2S_INT_TXDRQEN,
			   I2S_INT_TXDRQEN);
}


static void sun8i_i2s_stop_playback(struct priv *priv)
{
	/* Clear TX counter */
	regmap_write(priv->regmap, I2S_TXCNT, 0);

	/* Flush TX FIFO */
	regmap_update_bits(priv->regmap, I2S_FCTL,
			   I2S_FCTL_FTX,
			   I2S_FCTL_FTX);

	/* Disable TX Block */
	regmap_update_bits(priv->regmap, I2S_CTL,
			   I2S_CTL_TXEN,
			   0);

	/* Disable TX DRQ */
	regmap_update_bits(priv->regmap, I2S_INT,
			   I2S_INT_TXDRQEN,
			   0);
}

static int sun8i_i2s_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);

	DBGOUT("%s: cmd = %d, substream->stream = %d\n",
	       __func__, cmd, substream->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			sun8i_i2s_start_playback(priv);
		} else { // TODO
			return -EINVAL;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			sun8i_i2s_stop_playback(priv);
		} else { // TODO
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops sun8i_i2s_dai_ops = {
	.hw_params	= sun8i_i2s_hw_params,
	.set_fmt	= sun8i_i2s_set_fmt,
	.shutdown	= sun8i_i2s_shutdown,
	.startup	= sun8i_i2s_startup,
	.trigger	= sun8i_i2s_trigger,
};

static int sun8i_i2s_controls_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(cpu_dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);

	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int shift = *((unsigned int*)&mc->shift);
	unsigned int ret = 0;

	if (regmap_read(priv->regmap, mc->reg, &ret) != 0)
		return -EINVAL;

	ret &= shift;
	ucontrol->value.integer.value[0] = *((int*)&ret);
	return 0;
}

static int sun8i_i2s_controls_set(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(cpu_dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);

	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int value = *((unsigned int*)ucontrol->value.integer.value);
	unsigned int shift = *((unsigned int*)&mc->shift);
	unsigned int max = *((unsigned int*)&mc->max);

	if (value > max)
		return -EINVAL;

	regmap_update_bits(priv->regmap, mc->reg,
			   shift,
			   value);
	return 0;
}

static const struct snd_kcontrol_new sun8i_i2s_controls[] = {
	SOC_SINGLE_EXT("Mute Switch", I2S_CTL, I2S_CTL_OUTMUTE,
		       I2S_CTL_OUTMUTE, 0,
		       sun8i_i2s_controls_get, sun8i_i2s_controls_set),
};

static int sun8i_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct priv *priv = snd_soc_card_get_drvdata(card);

	snd_soc_dai_init_dma_data(dai, &priv->playback_dma_data, NULL);
	snd_soc_add_dai_controls(dai, sun8i_i2s_controls, ARRAY_SIZE(sun8i_i2s_controls));

	return 0;
}

static struct snd_soc_dai_driver sun8i_i2s_dai = {
	.name = "sun8i-i2s",
	.probe = sun8i_i2s_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT,
		.rate_min = 8000,
		.rate_max = 192000,
		.formats = I2S_FORMATS,
	},
	.ops = &sun8i_i2s_dai_ops,
	.symmetric_rates = 1,
};

static const struct snd_soc_component_driver sun8i_i2s_component = {
	.name			= "sun8i-i2s-comp",
};

/* --- dma --- */

static const struct snd_pcm_hardware sun8i_i2s_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = I2S_FORMATS,
	.rates = SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT,
	.rate_min = 8000,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = 8,
	.buffer_bytes_max = 1024 * 1024,
	.period_bytes_min = 156,
	.period_bytes_max = 1024 * 1024,
	.periods_min = 1,
	.periods_max = 8,
	.fifo_size = 128,
};

static const struct snd_dmaengine_pcm_config sun8i_i2s_config = {
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.pcm_hardware = &sun8i_i2s_pcm_hardware,
	.prealloc_buffer_size = 1024 * 1024,
};

/* --- audio card --- */

static struct device_node *sun8i_get_codec_by_remote_port(struct device *dev)
{
	struct device_node *ep, *remote;

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep)
		return NULL;
	remote = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);

	return remote;
}

static struct device_node *sun8i_get_codec(struct device *dev)
{
	struct of_phandle_args args;
	int ret;

	ret = of_parse_phandle_with_args(dev->of_node, "sound-dai",
					 "#sound-dai-cells", 0, &args);
	if (ret) {
		DBGOUT("%s: Can't find codec by \"sound-dai\", try \"remote-port\" ...\n", __func__);
		return sun8i_get_codec_by_remote_port(dev);
	}

	return args.np;
}

static int snd_sun8i_dac_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static int snd_sun8i_dac_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	return 0;
}

static struct snd_soc_ops snd_sun8i_dac_ops = {
        .hw_params = snd_sun8i_dac_hw_params,
};

static const struct snd_soc_dai_link snd_sun8i_dac_dai = {
	.name		= "sun8i-i2s-dac",
	.stream_name	= "sun8i-i2s-dac",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &snd_sun8i_dac_ops,
	.init		= snd_sun8i_dac_init,
};

static const struct snd_soc_card snd_sun8i_dac = {
	.name		= "snd-sun8i-i2s-dac",
};

static int sun8i_card_create(struct device *dev, struct priv *priv)
{
	struct snd_soc_card *card;
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_dai_link_component *codec;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	dai_link = devm_kzalloc(dev, sizeof(*dai_link), GFP_KERNEL);
	if (!dai_link)
		return -ENOMEM;
	codec = devm_kzalloc(dev, sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	codec->of_node = sun8i_get_codec(dev);
	if (!codec->of_node) {
		dev_err(dev, "no port node\n");
		return -ENXIO;
	}
	DBGOUT("%s: codec_name=\"%s\"\n", __func__, codec->of_node->name);

	if(snd_soc_of_get_dai_name(dev->of_node, &codec->dai_name) < 0)
	{
		dev_err(dev, "%s: failed to find dai name, use codec's name as dai name.\n", __func__);
		codec->dai_name = codec->of_node->name;
	}
	DBGOUT("%s: dai_name=\"%s\"\n", __func__, codec->dai_name);

	card->name = snd_sun8i_dac.name;
	card->dai_link = dai_link;
	card->num_links = 1;

	dai_link->name = snd_sun8i_dac_dai.name;
	dai_link->stream_name = snd_sun8i_dac_dai.stream_name;
	dai_link->dai_fmt = snd_sun8i_dac_dai.dai_fmt;
	dai_link->ops = snd_sun8i_dac_dai.ops;
	dai_link->init = snd_sun8i_dac_dai.init; 
	dai_link->cpu_of_node = dev->of_node;
	dai_link->platform_of_node = dev->of_node;

	dai_link->codecs = codec;
	dai_link->num_codecs = 1;

	card->dev = dev;
	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, priv);

	return devm_snd_soc_register_card(dev, card);
}

/* --- regmap --- */

static bool sun8i_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_TXFIFO:
		return false;

	default:
		return true;
	}
}

static bool sun8i_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_RXFIFO:
	case I2S_FSTA:
		return false;

	default:
		return true;
	}
}

static bool sun8i_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_RXFIFO:
	case I2S_FSTA:
	case I2S_TXCNT:
	case I2S_RXCNT:
		return true;

	default:
		return false;
	}
}

static const struct reg_default sun8i_i2s_reg_h3_defaults[] = {
	{ I2S_CTL, 0x00060000 }, // BCLK & LRCK for output
	{ I2S_FAT0, 0x00000033 }, 
	{ I2S_FAT1, 0x00000030 },
	{ I2S_FCTL, 0x000400f0 },
	{ I2S_INT, 0x00000000 },
	{ I2S_CLKD, 0x00000000 },
	{ I2S_TX0CHSEL_H3, 0x00000001 }, // 2 channels
	{ I2S_TX0CHMAP_H3, 0x76543210 },
	{ I2S_RXCHSEL_H3, 0x00000001 }, // 2 channels
	{ I2S_RXCHMAP_H3, 0x00000000 },
};

static const struct regmap_config sun8i_i2s_regmap_h3_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= I2S_RXCHMAP_H3,

	.cache_type		= REGCACHE_FLAT,
	.reg_defaults		= sun8i_i2s_reg_h3_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun8i_i2s_reg_h3_defaults),
	.writeable_reg		= sun8i_i2s_wr_reg,
	.readable_reg		= sun8i_i2s_rd_reg,
	.volatile_reg		= sun8i_i2s_volatile_reg,
};

static const struct reg_default sun8i_i2s_reg_a83t_defaults[] = {
	// TODO
	{ I2S_CTL, 0x00000000 },
};

static const struct regmap_config sun8i_i2s_regmap_a83t_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= I2S_TXCHMAP_A83T, // TODO

	.cache_type		= REGCACHE_FLAT,
	.reg_defaults		= sun8i_i2s_reg_a83t_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun8i_i2s_reg_a83t_defaults),
	.writeable_reg		= sun8i_i2s_wr_reg,
	.readable_reg		= sun8i_i2s_rd_reg,
	.volatile_reg		= sun8i_i2s_volatile_reg,
};

/* --- pm --- */

static int sun8i_i2s_runtime_resume(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct priv *priv = snd_soc_card_get_drvdata(card); 
	int ret;

	if (!IS_ERR_OR_NULL(priv->bus_clk)) {
		ret = clk_prepare_enable(priv->bus_clk);
		if (ret) {
			dev_err(dev, "Failed to enable bus clock\n");
			return ret;
		}
	}

	regcache_cache_only(priv->regmap, false);

	ret = regcache_sync(priv->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap cache\n");
		goto err_disable_clk;
	}

	return 0;

err_disable_clk:
	if (!IS_ERR_OR_NULL(priv->bus_clk))
		clk_disable_unprepare(priv->bus_clk);
	return ret;
}

static int sun8i_i2s_runtime_suspend(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct priv *priv = snd_soc_card_get_drvdata(card); 

	regcache_cache_only(priv->regmap, true);
	regcache_mark_dirty(priv->regmap);

	if (!IS_ERR_OR_NULL(priv->bus_clk))
		clk_disable_unprepare(priv->bus_clk);

	return 0;
}

/* --- module init --- */

static int sun8i_i2s_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct priv *priv;
	struct resource *res;
	void __iomem *mmio;
	int irq, ret;

	if (!dev->of_node) {
		dev_err(dev, "no DT!\n");
		return -EINVAL;
	}
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* get the resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(mmio)) {
		dev_err(dev, "Can't request IO region\n");
		return PTR_ERR(mmio);
	}
	DBGOUT("%s: resource=%pR, name=%s, mmio=%p\n", __func__, res, res->name, mmio);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Can't retrieve interrupt\n");
		return irq;
	}

	/* get SoC type */
	priv->type = (int) of_match_device(sun8i_i2s_of_match, &pdev->dev)->data;
	DBGOUT("%s: priv->type = %d\n", __func__, priv->type);

	/* get and enable the clocks */
	priv->bus_clk = devm_clk_get(dev, "apb");	/* optional */
	priv->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(priv->mod_clk)) {
		dev_err(dev, "Can't get mod clock\n");
		return PTR_ERR(priv->mod_clk);
	}
	ret = clk_set_rate(priv->mod_clk, 24576000);
	if (ret) {
		dev_err(dev, "Can't set rate of i2s clock\n");
		return ret;
	}

	if (priv->type == SOC_A83T)
		priv->regmap = devm_regmap_init_mmio(dev, mmio, &sun8i_i2s_regmap_a83t_config);
	else
		priv->regmap = devm_regmap_init_mmio(dev, mmio, &sun8i_i2s_regmap_h3_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "Regmap initialisation failed\n");
		return PTR_ERR(priv->regmap);
	}

	priv->rstc = devm_reset_control_get_optional(dev, NULL);
	if (!IS_ERR(priv->rstc)) {
		ret = reset_control_deassert(priv->rstc);
		if (ret < 0)
			return ret;
	}

	priv->playback_dma_data.maxburst = 8;
	priv->playback_dma_data.addr = res->start + I2S_TXFIFO;
	priv->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = sun8i_i2s_runtime_resume(dev);
		if (ret)
			goto err_pm_disable;
	}

	/* activate the audio subsystem */
	sun8i_i2s_init(priv);

	ret = devm_snd_soc_register_component(dev,
					      &sun8i_i2s_component,
					      &sun8i_i2s_dai, 1);
	if (ret) {
		dev_err(dev, "Could not register DAI, error_code = %d\n", ret);
		goto err_suspend;
	}

	ret = devm_snd_dmaengine_pcm_register(dev, &sun8i_i2s_config, 0);
	if (ret) {
		dev_err(dev, "Could not register PCM, error_code = %d\n", ret);
		goto err_suspend;
	}

	ret = sun8i_card_create(dev, priv);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "register card failed %d\n", ret);
		goto err_suspend;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(dev))
		sun8i_i2s_runtime_suspend(dev);
err_pm_disable:
	pm_runtime_disable(dev);

	if (!IS_ERR_OR_NULL(priv->rstc))
		reset_control_assert(priv->rstc);

	return ret;
}

static int sun8i_i2s_dev_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = dev_get_drvdata(&pdev->dev);
	struct priv *priv = snd_soc_card_get_drvdata(card); 

	snd_dmaengine_pcm_unregister(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun8i_i2s_runtime_suspend(&pdev->dev);

	if (!IS_ERR_OR_NULL(priv->rstc))
		reset_control_assert(priv->rstc);

	return 0;
}

static const struct dev_pm_ops sun8i_i2s_pm_ops = {
	.runtime_resume		= sun8i_i2s_runtime_resume,
	.runtime_suspend	= sun8i_i2s_runtime_suspend,
};

static struct platform_driver sun8i_i2s_driver = {
	.probe  = sun8i_i2s_dev_probe,
	.remove = sun8i_i2s_dev_remove,
	.driver = {
		.name = "sun8i-i2s",
		.of_match_table = sun8i_i2s_of_match,
		.pm = &sun8i_i2s_pm_ops,
	},
};
module_platform_driver(sun8i_i2s_driver);

MODULE_AUTHOR("Jean-Francois Moine <moinejf at free.fr>");
MODULE_AUTHOR("Anthony Lee <don.anthony.lee at gmail.com>");
MODULE_DESCRIPTION("Allwinner sun8i I2S ASoC Interface");
MODULE_LICENSE("GPL v2");
