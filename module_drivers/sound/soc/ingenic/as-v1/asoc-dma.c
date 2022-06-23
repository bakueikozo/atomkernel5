/*
 *  sound/soc/ingenic/asoc-dma.c
 *  ALSA Soc Audio Layer -- ingenic audio dma platform driver
 *
 *  Copyright 2014 Ingenic Semiconductor Co.,Ltd
 *	cli <chen.li@ingenic.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/delay.h>
#include "asoc-dma.h"

static int asoc_dma_debug = 0;
module_param(asoc_dma_debug, int, 0644);

#define DMA_DEBUG_MSG(msg...)			\
	do {					\
		if (asoc_dma_debug)		\
			printk(KERN_DEBUG"ADMA: " msg);	\
	} while(0)
#define DMA_SUBSTREAM_MSG(substream, msg...)	\
	do {					\
		if (asoc_dma_debug) {		\
			printk(KERN_DEBUG"ADMA[%s][%s]:", \
					substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? \
					"replay" : "record",	\
					substream->pcm->id);	\
			printk(KERN_DEBUG msg);		\
		} \
	} while(0)

#define INGENIC_DMA_BUFFERSIZE_PREALLOC (32 * 1024)
#define INGENIC_DMA_BUFFERSIZE_MAX (128 * 1024)
struct ingenic_dma_pcm {
	struct dma_chan *chan[SNDRV_PCM_STREAM_LAST + 1];
	struct snd_soc_component_driver dai;
	const struct snd_pcm_hardware *hardware;
	unsigned int prealloc_buffersize;
	int no_residue;
};
#define soc_platform_to_ingenicpcm(x) container_of(x, struct ingenic_dma_pcm, platform)

static int ingenic_pcm_hw_params(struct snd_soc_component *component,struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd,0);
	struct snd_dmaengine_dai_dma_data *dma_data = snd_soc_dai_get_dma_data(cpu_dai, substream);
	struct dma_slave_config slave_config;
	int ret;

	DMA_SUBSTREAM_MSG(substream, "%s enter\n", __func__);

	memset(&slave_config, 0, sizeof(slave_config));

	ret = snd_hwparams_to_dma_slave_config(substream, params, &slave_config);
	if (ret)
		return ret;
	snd_dmaengine_pcm_set_config_from_dai_data(substream, dma_data, &slave_config);

	ret = dmaengine_slave_config(prtd->dma_chan, &slave_config);
	if (ret)
		return ret;

	while (atomic_read(&prtd->wait_stopdma))
		msleep(10);

	prtd->stopdma_delayed_jiffies =  2 * (params_period_size(params) * HZ /params_rate(params));
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static int ingenic_pcm_hw_free(struct snd_soc_component *component,struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);

	DMA_SUBSTREAM_MSG(substream, "%s enter\n", __func__);

	while (atomic_read(&prtd->wait_stopdma))
		msleep(10);

	return snd_pcm_lib_free_pages(substream);
}

static int ingenic_pcm_prepare(struct snd_soc_component* component,struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);

	DMA_SUBSTREAM_MSG(substream, "%s enter\n", __func__);

	while (atomic_read(&prtd->wait_stopdma))
		msleep(10);

	return 0;
}

static void ingenic_pcm_dma_complete(void *data)
{
	struct snd_pcm_substream *substream = data;
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);

	DMA_SUBSTREAM_MSG(substream,"%s enter period interrupt %d\n", __func__,
			atomic_read(&prtd->wait_stopdma));

	if (!atomic_dec_if_positive(&prtd->wait_stopdma)) {
		struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
		struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd,0);
		dmaengine_terminate_all(prtd->dma_chan);
		if (cpu_dai->driver->ops->trigger)
			cpu_dai->driver->ops->trigger(substream, SNDRV_PCM_TRIGGER_STOP, cpu_dai);
		cancel_delayed_work(&prtd->stopdma_delayed_work);
		return;
	}

	if (prtd->file) {
		prtd->copy_start = substream->runtime->dma_area + prtd->pos;
		prtd->copy_length = snd_pcm_lib_period_bytes(substream);
		if (work_pending(&prtd->debug_work))
			printk(KERN_WARNING"debug file %s may loss data\n", prtd->file_name);
		schedule_work(&prtd->debug_work);
	}

	prtd->pos += snd_pcm_lib_period_bytes(substream);
	if (prtd->pos >= snd_pcm_lib_buffer_bytes(substream))
		prtd->pos = 0;
	snd_pcm_period_elapsed(substream);
	return;
}

static int ingenic_pcm_prepare_and_submit(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);
	enum dma_transfer_direction direction;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags = DMA_CTRL_ACK;

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	prtd->pos = 0;
	desc = dmaengine_prep_dma_cyclic(prtd->dma_chan,
			substream->runtime->dma_addr,
			snd_pcm_lib_buffer_bytes(substream),
			snd_pcm_lib_period_bytes(substream), direction, flags);
	if (!desc) {
		dev_err(rtd->dev, "cannot prepare slave dma\n");
		return -ENOMEM;
	}

	desc->callback = ingenic_pcm_dma_complete;
	desc->callback_param = substream;
	prtd->cookie = dmaengine_submit(desc);
	return 0;
}

static int ingenic_pcm_trigger(struct snd_soc_component *component,struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);
	int ret;

	DMA_SUBSTREAM_MSG(substream,"%s enter cmd %d\n", __func__, cmd);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (atomic_read(&prtd->wait_stopdma))
			return -EPIPE;

		ret = ingenic_pcm_prepare_and_submit(substream);
		if (ret)
			return ret;
		dma_async_issue_pending(prtd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dmaengine_resume(prtd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (runtime->info & SNDRV_PCM_INFO_PAUSE)
			dmaengine_pause(prtd->dma_chan);
		else
			dmaengine_terminate_all(prtd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_pause(prtd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (atomic_read(&prtd->wait_stopdma))
			schedule_delayed_work(&prtd->stopdma_delayed_work,
					prtd->stopdma_delayed_jiffies);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t ingenic_pcm_pointer_no_residue(struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);

	return bytes_to_frames(substream->runtime, prtd->pos);
}

static snd_pcm_uframes_t ingenic_pcm_pointer_residue(struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);
	struct dma_tx_state state;
	enum dma_status status;
	size_t buf_size;
	ssize_t pos = 0;

	status = dmaengine_tx_status(prtd->dma_chan, prtd->cookie, &state);

	if (status == DMA_IN_PROGRESS || status == DMA_PAUSED) {
		buf_size = snd_pcm_lib_buffer_bytes(substream);
		if (state.residue > 0 && state.residue <= buf_size)
			pos = buf_size - state.residue;
	}

	DMA_SUBSTREAM_MSG(substream, "prtd->pos %x really pos %x\n", prtd->pos, pos);
	return bytes_to_frames(substream->runtime, pos);
}

static snd_pcm_uframes_t ingenic_pcm_pointer(struct snd_soc_component *component,struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct ingenic_dma_pcm *ingenic_pcm = snd_soc_component_get_drvdata(component);

	if (ingenic_pcm->no_residue)
		return ingenic_pcm_pointer_no_residue(substream);

	return ingenic_pcm_pointer_residue(substream);
}

static void debug_work(struct work_struct *debug_work)
{
	struct ingenic_pcm_runtime_data *prtd =
		container_of(debug_work, struct ingenic_pcm_runtime_data, debug_work);
	if (!IS_ERR_OR_NULL(prtd->file)) {
		prtd->old_fs = get_fs();
		set_fs(KERNEL_DS);
		vfs_write(prtd->file, prtd->copy_start,
				prtd->copy_length,
				&prtd->file_offset);
		prtd->file_offset = prtd->file->f_pos;
		set_fs(prtd->old_fs);
	}
}

static void ingenic_pcm_open_debug_file(struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);
#ifdef CONFIG_ANDROID
	char *file_dir = "/data";
#else
	char *file_dir = "/tmp";
#endif
	prtd->file_name = kzalloc(40*sizeof(char), GFP_KERNEL);
	if (prtd->file_name) {
		sprintf(prtd->file_name, "%s/%sd%is%i.pcm",
				file_dir,
				substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
				"replay" : "record",
				substream->pcm->device,
				substream->number);
		pr_info("open debug file %s \n", prtd->file_name);
		prtd->file = filp_open(prtd->file_name,
				O_RDWR | O_APPEND /*| O_CREAT*/, S_IRUSR | S_IWUSR);
		if (IS_ERR(prtd->file)) {
			kfree(prtd->file_name);
			prtd->file_name = NULL;
			prtd->file = NULL;
			return;
		}
		pr_info("open debug %s success (Poor performance)\n",
				prtd->file_name);
		prtd->file_offset =prtd->file->f_pos;
		INIT_WORK(&prtd->debug_work, debug_work);
	}
	return;
}

static void ingenic_pcm_close_debug_file(struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);

	if (!prtd || !prtd->file)
		return;

	flush_work(&prtd->debug_work);
	filp_close(prtd->file, NULL);
	kfree(prtd->file_name);
	return;
}

static void stopdma_delayed_work(struct work_struct *work)
{
	struct ingenic_pcm_runtime_data *prtd = container_of(work, struct ingenic_pcm_runtime_data,
			stopdma_delayed_work.work);

	DMA_SUBSTREAM_MSG(prtd->substream,"%s delayed work stop dma %d\n", __func__, atomic_read(&prtd->wait_stopdma));
	if (!atomic_dec_if_positive(&prtd->wait_stopdma)) {
		struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(prtd->substream);
		struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd,0);
		dmaengine_terminate_all(prtd->dma_chan);
		if (cpu_dai->driver->ops->trigger)
			cpu_dai->driver->ops->trigger(prtd->substream, SNDRV_PCM_TRIGGER_STOP, cpu_dai);
	}
	return;
}

static int ingenic_pcm_open(struct snd_soc_component *component,struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct snd_soc_component_driver *dai = component->driver;
	struct ingenic_dma_pcm *ingenic_pcm = container_of(dai, struct ingenic_dma_pcm, dai);
	struct dma_chan *chan = ingenic_pcm->chan[substream->stream];
	struct dma_slave_caps dma_caps;
	struct snd_pcm_hardware hw;
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct ingenic_pcm_runtime_data *prtd = NULL;
	u32 addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	int ret;

	DMA_DEBUG_MSG("%s enter\n", __func__);
	if (ingenic_pcm->hardware) {
		ret = snd_soc_set_runtime_hwparams(substream, ingenic_pcm->hardware);
		if (ret)
			return ret;
	} else {
		int i;
		struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd,0);
		dma_data = snd_soc_dai_get_dma_data(cpu_dai, substream);
		memset(&hw, 0, sizeof(hw));
		hw.buffer_bytes_max = INGENIC_DMA_BUFFERSIZE_MAX;
		hw.period_bytes_max = (32 * 1024);
		hw.period_bytes_min = 1024;
		hw.periods_max = 64;	/*INGENIC PDMA LIMITE*/
		hw.periods_min = 4;
		hw.fifo_size = dma_data->fifo_size;
		hw.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED;
		ret = dma_get_slave_caps(chan, &dma_caps);
		if (ret == 0) {
			if (dma_caps.residue_granularity <= DMA_RESIDUE_GRANULARITY_SEGMENT)
				hw.info |= SNDRV_PCM_INFO_BATCH;
			if (dma_caps.residue_granularity == DMA_RESIDUE_GRANULARITY_DESCRIPTOR)
				ingenic_pcm->no_residue = 1;
			if (dma_caps.cmd_pause)
				hw.info |= SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME;
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				addr_widths = dma_caps.dst_addr_widths;
			else
				addr_widths = dma_caps.src_addr_widths;
		}

		for (i = 0; i <= SNDRV_PCM_FORMAT_LAST; i++) {
			int bits = snd_pcm_format_physical_width(i);
			switch (bits) {
			case 8:
			case 16:
			case 24:
			case 32:
			case 64:
				if (addr_widths & (1 << (bits / 8)))
					hw.formats |= (1LL << i);
				break;
			default:
				/* Unsupported types */
				break;
			}
		}

		ret = snd_soc_set_runtime_hwparams(substream, &hw);
		if (ret)
			return ret;
	}

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (!prtd)
		return -ENOMEM;

	prtd->dma_chan = chan;
	prtd->substream = substream;
	INIT_DELAYED_WORK(&prtd->stopdma_delayed_work, stopdma_delayed_work);
	prtd->stopdma_delayed_jiffies = 0;
	atomic_set(&prtd->wait_stopdma, 0);
	substream->runtime->private_data = prtd;

//	ingenic_pcm_open_debug_file(substream);
	return 0;
}

static int ingenic_pcm_close(struct snd_soc_component *component,struct snd_pcm_substream *substream)
{
	struct ingenic_pcm_runtime_data *prtd = substream_to_prtd(substream);

	DMA_DEBUG_MSG("%s enter\n", __func__);

	BUG_ON(atomic_read(&prtd->wait_stopdma));
	flush_delayed_work(&prtd->stopdma_delayed_work);

	ingenic_pcm_close_debug_file(substream);

	substream->private_data = NULL;

	kfree(prtd);
	return 0;
}

/* struct snd_pcm_ops ingenic_pcm_ops = { */
/* 	.open		= ingenic_pcm_open, */
/* 	.close		= ingenic_pcm_close, */
/* 	.ioctl		= snd_pcm_lib_ioctl, */
/* 	.hw_params	= ingenic_pcm_hw_params, */
/* 	.hw_free	= ingenic_pcm_hw_free, */
/* 	.prepare	= ingenic_pcm_prepare, */
/* 	.trigger	= ingenic_pcm_trigger, */
/* 	.pointer	= ingenic_pcm_pointer, */
/* }; */


static void ingenic_pcm_free(struct snd_soc_component *component, struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int ingenic_pcm_new(struct snd_soc_component *component,struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component_driver *dai = component->driver;
	struct ingenic_dma_pcm *ingenic_pcm = container_of(dai, struct ingenic_dma_pcm, dai);
	struct snd_pcm_substream *substream;
	int ret = -EINVAL;
	int i;

	DMA_DEBUG_MSG("%s enter\n", __func__);
	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		if (!ingenic_pcm->chan[i]) {
			dev_err(component->dev,
					"Missing dma channel for stream: %d\n", i);
			ret = -EINVAL;
			goto err_free;
		}

		snd_pcm_lib_preallocate_pages(substream,
				SNDRV_DMA_TYPE_DEV_IRAM,
				ingenic_pcm->chan[i]->device->dev,
				ingenic_pcm->prealloc_buffersize,
				ingenic_pcm->hardware ?
				ingenic_pcm->hardware->buffer_bytes_max :
				INGENIC_DMA_BUFFERSIZE_MAX);
	}
	return 0;

err_free:
	dev_err(rtd->dev, "Failed to alloc dma buffer %d\n", ret);
	ingenic_pcm_free(component, rtd->pcm);
	return ret;
}

 static struct snd_soc_component_driver ingenic_pcm_platform_component = {
 	.open				= ingenic_pcm_open,
 	.close				= ingenic_pcm_close,
 	.hw_params			= ingenic_pcm_hw_params,
 	.hw_free			= ingenic_pcm_hw_free,
 	.prepare			= ingenic_pcm_prepare,
 	.trigger			= ingenic_pcm_trigger,
 	.pointer			= ingenic_pcm_pointer,
 	.pcm_construct        = ingenic_pcm_new,
 	.pcm_destruct       = ingenic_pcm_free,
 };

static void ingenic_pcm_release_dma_channel(struct ingenic_dma_pcm *ingenic_pcm)
{
	int i;

	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
		if (!ingenic_pcm->chan[i])
			continue;
		dma_release_channel(ingenic_pcm->chan[i]);
	}

	return;
}

int ingenic_dma_pcm_register(struct device *dev,  const struct snd_dmaengine_pcm_config *config, struct ingenic_dma_pcm *ingenic_pcm)
{
	struct dma_chan *chan;
	const char *name;
	int ret, i;
	const char * const ingenic_pcm_dma_channel_names[] = {
		[SNDRV_PCM_STREAM_PLAYBACK] = "tx",
		[SNDRV_PCM_STREAM_CAPTURE] = "rx",
	};

	ingenic_pcm = devm_kzalloc(dev, sizeof(struct ingenic_dma_pcm), GFP_KERNEL);
	if (!ingenic_pcm)
		return -ENOMEM;

	if (config && config->pcm_hardware)
		ingenic_pcm->hardware = config->pcm_hardware;

	if (config && config->prealloc_buffer_size)
		ingenic_pcm->prealloc_buffersize = config->prealloc_buffer_size;
	else
		ingenic_pcm->prealloc_buffersize = INGENIC_DMA_BUFFERSIZE_PREALLOC;

	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
		if (config && config->chan_names[i])
			name = config->chan_names[i];
		else
			name = ingenic_pcm_dma_channel_names[i];
		chan = dma_request_slave_channel(dev, name);
		if (IS_ERR(chan)) {
			if (PTR_ERR(chan) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			ingenic_pcm->chan[i] = NULL;
			dev_info(dev, "%s channel request failed\n", name);
		} else {
			ingenic_pcm->chan[i] = chan;
		}
	}

	memcpy(&ingenic_pcm->dai, &ingenic_pcm_platform_component, sizeof(struct snd_soc_component_driver));

	ret = devm_snd_soc_register_component(dev, &ingenic_pcm->dai,NULL,0);
	if (ret) {
		ingenic_pcm_release_dma_channel(ingenic_pcm);
		dev_err(dev, "Failed to register platfrom\n");
		return ret;
	}
	dev_info(dev, "Audio dma platfrom probe success\n");
	return 0;
}
EXPORT_SYMBOL_GPL(ingenic_dma_pcm_register);

void ingenic_dma_pcm_unregister(struct ingenic_dma_pcm *ingenic_pcm)
{
	ingenic_pcm_release_dma_channel(ingenic_pcm);
	kfree(ingenic_pcm);
	return;
}
EXPORT_SYMBOL_GPL(ingenic_dma_pcm_unregister);

MODULE_DESCRIPTION("INGENIC ASOC Platform driver");
MODULE_AUTHOR("cli<chen.li@ingenic.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ingenic-asoc-dma");
