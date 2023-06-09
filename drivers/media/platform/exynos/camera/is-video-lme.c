// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Exynos Pablo image subsystem functions
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <videodev2_exynos_camera.h>
#include <linux/v4l2-mediabus.h>
#include <linux/pm_qos.h>
#include <linux/bug.h>

#include "is-core.h"
#include "is-cmd.h"
#include "is-err.h"
#include "is-video.h"

const struct v4l2_file_operations is_lme_video_fops;
const struct v4l2_ioctl_ops is_lme_video_ioctl_ops;
const struct vb2_ops is_lme_qops;

int is_lme_video_probe(void *data)
{
	int ret = 0;
	struct is_core *core;
	struct is_video *video;

	FIMC_BUG(!data);

	core = (struct is_core *)data;
	video = &core->video_lme;
	video->resourcemgr = &core->resourcemgr;

	if (!core->pdev) {
		probe_err("pdev is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = is_video_probe(video,
		IS_VIDEO_LME_NAME,
		IS_VIDEO_LME_NUM,
		VFL_DIR_M2M,
		&core->resourcemgr.mem,
		&core->v4l2_dev,
		&is_lme_video_fops,
		&is_lme_video_ioctl_ops);
	if (ret)
		dev_err(&core->pdev->dev, "%s is fail(%d)\n", __func__, ret);

p_err:
	return ret;
}


/*
 * =============================================================================
 * Video File Opertation
 * =============================================================================
 */

static int is_lme_video_close(struct file *file)
{
	int ret = 0;
	int refcount;
	struct is_video_ctx *vctx;
	struct is_video *video;
	struct is_device_ischain *device;

	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG(!GET_VIDEO(vctx));
	video = GET_VIDEO(vctx);

	ret = is_ischain_lme_close(device, vctx);
	if (ret)
		merr("is_ischain_lme_close is fail(%d)", device, ret);

	ret = is_video_close(vctx);
	if (ret)
		merr("is_video_close is fail(%d)", device, ret);

	refcount = close_vctx(file, video, vctx);
	if (refcount < 0)
		merr("close_vctx is fail(%d)", device, refcount);

	minfo("[LME] %s(%d,%d):%d\n", device, __func__, atomic_read(&device->open_cnt), refcount, ret);

	return ret;
}

static unsigned int is_lme_video_poll(struct file *file,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!wait);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	ret = is_video_poll(file, vctx, wait);
	if (ret)
		merr("is_video_poll is fail(%d)", device, ret);

	return ret;
}

static int is_lme_video_mmap(struct file *file,
	struct vm_area_struct *vma)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!vma);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	ret = is_video_mmap(file, vctx, vma);
	if (ret)
		merr("is_video_mmap is fail(%d)", device, ret);

	return ret;
}

const struct v4l2_file_operations is_lme_video_fops = {
	.owner		= THIS_MODULE,
	//.open		= is_lme_video_open,
	.release	= is_lme_video_close,
	.poll		= is_lme_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= is_lme_video_mmap,
};

/*
 * =============================================================================
 * Video Ioctl Opertation
 * =============================================================================
 */

static int is_lme_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	struct is_video *video = video_drvdata(file);

	FIMC_BUG(!cap);
	FIMC_BUG(!video);

	snprintf(cap->driver, sizeof(cap->driver), "%s", video->vd.name);
	snprintf(cap->card, sizeof(cap->card), "%s", video->vd.name);
	cap->capabilities |= V4L2_CAP_STREAMING
				| V4L2_CAP_VIDEO_OUTPUT
				| V4L2_CAP_VIDEO_OUTPUT_MPLANE;
	cap->device_caps |= cap->capabilities;

	return 0;
}

static int is_lme_video_get_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	struct is_video_ctx *vctx;

	FIMC_BUG(!format);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	mdbgv_lme("%s\n", vctx, __func__);

	return 0;
}

static int is_lme_video_set_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!format);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	ret = is_video_set_format_mplane(file, vctx, format);
	if (ret) {
		merr("is_video_set_format_mplane is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static int is_lme_video_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *buf)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!buf);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mdbgv_lme("%s(buffers : %d)\n", vctx, __func__, buf->count);

	ret = is_video_reqbufs(file, vctx, buf);
	if (ret)
		merr("is_video_reqbufs is fail(error %d)", device, ret);

	return ret;
}

static int is_lme_video_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!buf);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	ret = is_video_querybuf(file, vctx, buf);
	if (ret)
		merr("is_video_querybuf is fail(%d)", device, ret);

	return ret;
}

static int is_lme_video_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;
	struct is_queue *queue;

	FIMC_BUG(!buf);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG(!GET_QUEUE(vctx));
	queue = GET_QUEUE(vctx);

	mvdbgs(3, "%s(index : %d)\n", vctx, &vctx->queue, __func__, buf->index);

	if (!test_bit(IS_QUEUE_STREAM_ON, &queue->state)) {
		merr("stream off state, can NOT qbuf", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = CALL_VOPS(vctx, qbuf, buf);
	if (ret)
		merr("qbuf is fail(%d)", device, ret);

p_err:
	return ret;
}

static int is_lme_video_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;
	bool blocking;

	FIMC_BUG(!buf);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;
	blocking = file->f_flags & O_NONBLOCK;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mvdbgs(3, "%s\n", vctx, &vctx->queue, __func__);

	ret = CALL_VOPS(vctx, dqbuf, buf, blocking);
	if (ret)
		merr("dqbuf is fail(%d)", device, ret);

	return ret;
}

static int is_lme_video_prepare(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;
	struct is_framemgr *framemgr;
	struct is_frame *frame;

	FIMC_BUG(!buf);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG(!GET_FRAMEMGR(vctx));
	framemgr = GET_FRAMEMGR(vctx);
	frame = &framemgr->frames[buf->index];

	ret = is_video_prepare(file, vctx, buf);
	if (ret) {
		merr("is_video_prepare is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	minfo("[LME] %s(%d):%d\n", device, __func__, buf->index, ret);
	return ret;
}

static int is_lme_video_streamon(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	ret = is_video_streamon(file, vctx, type);
	if (ret)
		merr("is_video_streamon is fail(%d)", device, ret);

	return ret;
}

static int is_lme_video_streamoff(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	ret = is_video_streamoff(file, vctx, type);
	if (ret)
		merr("is_video_streamoff is fail(%d)", device, ret);

	return ret;
}

static int is_lme_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	/* Todo : add to enum input control code */
	return 0;
}

static int is_lme_video_g_input(struct file *file, void *priv,
	unsigned int *input)
{
	/* Todo : add to get input control code */
	return 0;
}

static int is_lme_video_s_input(struct file *file, void *priv,
	unsigned int input)
{
	int ret = 0;
	u32 stream, position, vindex, intype, leader;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	stream = (input & INPUT_STREAM_MASK) >> INPUT_STREAM_SHIFT;
	position = (input & INPUT_POSITION_MASK) >> INPUT_POSITION_SHIFT;
	vindex = (input & INPUT_VINDEX_MASK) >> INPUT_VINDEX_SHIFT;
	intype = (input & INPUT_INTYPE_MASK) >> INPUT_INTYPE_SHIFT;
	leader = (input & INPUT_LEADER_MASK) >> INPUT_LEADER_SHIFT;

	mdbgv_lme("%s(input : %08X)[%d,%d,%d,%d,%d]\n", vctx, __func__, input,
			stream, position, vindex, intype, leader);

	ret = is_video_s_input(file, vctx);
	if (ret) {
		merr("is_video_s_input is fail(%d)", device, ret);
		goto p_err;
	}

	ret = is_ischain_lme_s_input(device, stream,
				position, vindex, intype, leader);
	if (ret) {
		merr("is_ischain_isp_s_input is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static int is_lme_video_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_video *video;
	struct is_device_ischain *device;

	FIMC_BUG(!ctrl);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG(!GET_VIDEO(vctx));
	video = GET_VIDEO(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	switch (ctrl->id) {
	case V4L2_CID_IS_FORCE_DONE:
		set_bit(IS_GROUP_REQUEST_FSTOP, &device->group_lme.state);
		break;
	default:
		ret = is_video_s_ctrl(file, vctx, ctrl);
		if (ret) {
			merr("is_video_s_ctrl is fail(%d)", device, ret);
			goto p_err;
		}
		break;
	}

p_err:
	return ret;
}

static int is_lme_video_g_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!ctrl);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	switch (ctrl->id) {
	default:
		merr("unsupported ioctl(%d)\n", device, ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int is_lme_video_g_ext_ctrl(struct file *file, void *priv,
	struct v4l2_ext_controls *ctrls)
{
	int ret = 0;
	struct v4l2_ext_control *ctrl;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG(!ctrls);
	FIMC_BUG(!file);
	FIMC_BUG(!file->private_data);
	vctx = file->private_data;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	ctrl = ctrls->controls;
	switch (ctrl->id) {
	default:
		merr("unsupported ioctl(%d)\n", device, ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct v4l2_ioctl_ops is_lme_video_ioctl_ops = {
	.vidioc_querycap		= is_lme_video_querycap,
	.vidioc_g_fmt_vid_out_mplane	= is_lme_video_get_format_mplane,
	.vidioc_s_fmt_vid_out_mplane	= is_lme_video_set_format_mplane,
	.vidioc_reqbufs			= is_lme_video_reqbufs,
	.vidioc_querybuf		= is_lme_video_querybuf,
	.vidioc_qbuf			= is_lme_video_qbuf,
	.vidioc_dqbuf			= is_lme_video_dqbuf,
	.vidioc_prepare_buf		= is_lme_video_prepare,
	.vidioc_streamon		= is_lme_video_streamon,
	.vidioc_streamoff		= is_lme_video_streamoff,
	.vidioc_enum_input		= is_lme_video_enum_input,
	.vidioc_g_input			= is_lme_video_g_input,
	.vidioc_s_input			= is_lme_video_s_input,
	.vidioc_s_ctrl			= is_lme_video_s_ctrl,
	.vidioc_g_ctrl			= is_lme_video_g_ctrl,
	.vidioc_g_ext_ctrls		= is_lme_video_g_ext_ctrl,
};

static int is_lme_queue_setup(struct vb2_queue *vbq,
	unsigned int *num_buffers, unsigned int *num_planes,
	unsigned int sizes[],
	struct device *alloc_devs[])
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;
	struct is_video *video;
	struct is_queue *queue;

	FIMC_BUG(!vbq);
	FIMC_BUG(!vbq->drv_priv);
	vctx = vbq->drv_priv;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG(!GET_VIDEO(vctx));
	video = GET_VIDEO(vctx);

	FIMC_BUG(!GET_QUEUE(vctx));
	queue = GET_QUEUE(vctx);

	mdbgv_isp("%s\n", vctx, __func__);

	ret = is_queue_setup(queue,
		video->alloc_ctx,
		num_planes,
		sizes,
		alloc_devs);
	if (ret)
		merr("is_queue_setup is fail(%d)", device, ret);

	return ret;
}

static int is_lme_start_streaming(struct vb2_queue *vbq,
	unsigned int count)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_queue *queue;
	struct is_device_ischain *device;

	FIMC_BUG(!vbq);
	FIMC_BUG(!vbq->drv_priv);
	vctx = vbq->drv_priv;

	FIMC_BUG(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG(!GET_QUEUE(vctx));
	queue = GET_QUEUE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	ret = is_queue_start_streaming(queue, device);
	if (ret) {
		merr("is_queue_start_streaming is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static void is_lme_stop_streaming(struct vb2_queue *vbq)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_queue *queue;
	struct is_device_ischain *device;

	FIMC_BUG_VOID(!vbq);
	FIMC_BUG_VOID(!vbq->drv_priv);
	vctx = vbq->drv_priv;

	FIMC_BUG_VOID(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG_VOID(!GET_QUEUE(vctx));
	queue = GET_QUEUE(vctx);

	mdbgv_lme("%s\n", vctx, __func__);

	ret = is_queue_stop_streaming(queue, device);
	if (ret) {
		merr("is_queue_stop_streaming is fail(%d)", device, ret);
		return;
	}
}

static void is_lme_buffer_queue(struct vb2_buffer *vb)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_queue *queue;
	struct is_device_ischain *device;

	FIMC_BUG_VOID(!vb);
	FIMC_BUG_VOID(!vb->vb2_queue->drv_priv);
	vctx = vb->vb2_queue->drv_priv;

	FIMC_BUG_VOID(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	FIMC_BUG_VOID(!GET_QUEUE(vctx));
	queue = GET_QUEUE(vctx);

	mvdbgs(3, "%s(%d)\n", vctx, &vctx->queue, __func__, vb->index);

	ret = is_queue_buffer_queue(queue, vb);
	if (ret) {
		merr("is_queue_buffer_queue is fail(%d)", device, ret);
		return;
	}

	ret = is_ischain_lme_buffer_queue(device, queue, vb->index);
	if (ret) {
		merr("is_ischain_lme_buffer_queue is fail(%d)", device, ret);
		return;
	}
}

static void is_lme_buffer_finish(struct vb2_buffer *vb)
{
	int ret = 0;
	struct is_video_ctx *vctx;
	struct is_device_ischain *device;

	FIMC_BUG_VOID(!vb);
	FIMC_BUG_VOID(!vb->vb2_queue->drv_priv);
	vctx = vb->vb2_queue->drv_priv;

	FIMC_BUG_VOID(!GET_DEVICE(vctx));
	device = GET_DEVICE(vctx);

	mvdbgs(3, "%s(%d)\n", vctx, &vctx->queue, __func__, vb->index);

	is_queue_buffer_finish(vb);

	ret = is_ischain_lme_buffer_finish(device, vb->index);
	if (ret) {
		merr("is_ischain_lme_buffer_finish is fail(%d)", device, ret);
		return;
	}
}

const struct vb2_ops is_lme_qops = {
	.queue_setup		= is_lme_queue_setup,
	.buf_init		= is_queue_buffer_init,
	.buf_cleanup		= is_queue_buffer_cleanup,
	.buf_prepare		= is_queue_buffer_prepare,
	.buf_queue		= is_lme_buffer_queue,
	.buf_finish		= is_lme_buffer_finish,
	.wait_prepare		= is_queue_wait_prepare,
	.wait_finish		= is_queue_wait_finish,
	.start_streaming	= is_lme_start_streaming,
	.stop_streaming		= is_lme_stop_streaming,
};
