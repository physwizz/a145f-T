/*
 * Samsung EXYNOS FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "is-hw-3aa.h"
#include "is-hw-mcscaler-v3.h"
#include "is-hw-isp.h"
#include "is-err.h"

extern struct is_lib_support gPtr_lib_support;

static int __nocfi is_hw_isp_open(struct is_hw_ip *hw_ip, u32 instance,
	struct is_group *group)
{
	int ret = 0;
	struct is_hw_isp *hw_isp = NULL;

	FIMC_BUG(!hw_ip);

	if (test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	frame_manager_probe(hw_ip->framemgr, hw_ip->id, "HWISP");
	frame_manager_open(hw_ip->framemgr, IS_MAX_HW_FRAME);

	hw_ip->priv_info = vzalloc(sizeof(struct is_hw_isp));
	if (!hw_ip->priv_info) {
		mserr_hw("hw_ip->priv_info(null)", instance, hw_ip);
		ret = -ENOMEM;
		goto err_alloc;
	}

	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;
#ifndef DISABLE_LIB
#ifdef ENABLE_FPSIMD_FOR_USER
	is_fpsimd_get_func();
	ret = get_lib_func(LIB_FUNC_ISP, (void **)&hw_isp->lib_func);
	is_fpsimd_put_func();
#else
	ret = get_lib_func(LIB_FUNC_ISP, (void **)&hw_isp->lib_func);
#endif
#endif

	if (hw_isp->lib_func == NULL) {
		mserr_hw("hw_isp->lib_func(null)", instance, hw_ip);
		is_load_clear();
		ret = -EINVAL;
		goto err_lib_func;
	}
	msinfo_hw("get_lib_func is set\n", instance, hw_ip);

	hw_isp->lib_support = &gPtr_lib_support;
	hw_isp->lib[instance].func = hw_isp->lib_func;

	ret = is_lib_isp_chain_create(hw_ip, &hw_isp->lib[instance], instance);
	if (ret) {
		mserr_hw("chain create fail", instance, hw_ip);
		ret = -EINVAL;
		goto err_chain_create;
	}

	set_bit(HW_OPEN, &hw_ip->state);
	msdbg_hw(2, "open: [G:0x%x], framemgr[%s]", instance, hw_ip,
		GROUP_ID(group->id), hw_ip->framemgr->name);

	return 0;

err_chain_create:
err_lib_func:
	vfree(hw_ip->priv_info);
	hw_ip->priv_info = NULL;
err_alloc:
	frame_manager_close(hw_ip->framemgr);
	return ret;
}

static int is_hw_isp_init(struct is_hw_ip *hw_ip, u32 instance,
	struct is_group *group, bool flag, u32 module_id)
{
	int ret = 0;
	struct is_hw_isp *hw_isp = NULL;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);
	FIMC_BUG(!group);

	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	hw_isp->lib[instance].object = NULL;
	hw_isp->lib[instance].func   = hw_isp->lib_func;
	hw_isp->param_set[instance].reprocessing = flag;

	if (hw_isp->lib[instance].object != NULL) {
		msdbg_hw(2, "object is already created\n", instance, hw_ip);
	} else {
		ret = is_lib_isp_object_create(hw_ip, &hw_isp->lib[instance],
				instance, (u32)flag, module_id);
		if (ret) {
			mserr_hw("object create fail", instance, hw_ip);
			return -EINVAL;
		}
	}

	set_bit(HW_INIT, &hw_ip->state);
	return ret;
}

static int is_hw_isp_deinit(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_isp *hw_isp;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);

	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	is_lib_isp_object_destroy(hw_ip, &hw_isp->lib[instance], instance);
	hw_isp->lib[instance].object = NULL;

	return ret;
}

static int is_hw_isp_close(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_isp *hw_isp;

	FIMC_BUG(!hw_ip);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;
	FIMC_BUG(!hw_isp->lib_support);

	is_lib_isp_chain_destroy(hw_ip, &hw_isp->lib[instance], instance);
	vfree(hw_ip->priv_info);
	hw_ip->priv_info = NULL;
	frame_manager_close(hw_ip->framemgr);

	clear_bit(HW_OPEN, &hw_ip->state);

	return ret;
}

static int is_hw_isp_enable(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	atomic_inc(&hw_ip->run_rsccount);
	set_bit(HW_RUN, &hw_ip->state);

	return ret;
}

static int is_hw_isp_disable(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;
	long timetowait;
	struct is_hw_isp *hw_isp;
	struct isp_param_set *param_set;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	msinfo_hw("isp_disable: Vvalid(%d)\n", instance, hw_ip,
		atomic_read(&hw_ip->status.Vvalid));

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;
	param_set = &hw_isp->param_set[instance];

	timetowait = wait_event_timeout(hw_ip->status.wait_queue,
		!atomic_read(&hw_ip->status.Vvalid),
		IS_HW_STOP_TIMEOUT);

	if (!timetowait) {
		mserr_hw("wait FRAME_END timeout (%ld)", instance,
			hw_ip, timetowait);
		ret = -ETIME;
	}

	param_set->fcount = 0;
	if (test_bit(HW_RUN, &hw_ip->state)) {
		/* TODO: need to kthread_flush when isp use task */
		is_lib_isp_stop(hw_ip, &hw_isp->lib[instance], instance);
	} else {
		msdbg_hw(2, "already disabled\n", instance, hw_ip);
	}

	if (atomic_dec_return(&hw_ip->run_rsccount) > 0)
		return 0;

	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

int is_hw_isp_set_yuv_range(struct is_hw_ip *hw_ip,
	struct isp_param_set *param_set, u32 fcount, ulong hw_map)
{
	int ret = 0;
	struct is_hw_ip *hw_ip_mcsc = NULL;
	struct is_hw_mcsc *hw_mcsc = NULL;
	enum is_hardware_id hw_id = DEV_HW_END;
	int hw_slot = 0;
	int yuv_range = 0; /* 0: FULL, 1: NARROW */

#if !defined(USE_YUV_RANGE_BY_ISP)
	return 0;
#endif
	if (test_bit(DEV_HW_MCSC0, &hw_map))
		hw_id = DEV_HW_MCSC0;
	else if (test_bit(DEV_HW_MCSC1, &hw_map))
		hw_id = DEV_HW_MCSC1;

	hw_slot = is_hw_slot_id(hw_id);
	if (valid_hw_slot_id(hw_slot)) {
		hw_ip_mcsc = &hw_ip->hardware->hw_ip[hw_slot];
		FIMC_BUG(!hw_ip_mcsc->priv_info);
		hw_mcsc = (struct is_hw_mcsc *)hw_ip_mcsc->priv_info;
		yuv_range = hw_mcsc->yuv_range;
	}

	if (yuv_range == SCALER_OUTPUT_YUV_RANGE_NARROW) {
		switch (param_set->otf_output.format) {
		case OTF_OUTPUT_FORMAT_YUV444:
			param_set->otf_output.format = OTF_OUTPUT_FORMAT_YUV444_TRUNCATED;
			break;
		case OTF_OUTPUT_FORMAT_YUV422:
			param_set->otf_output.format = OTF_OUTPUT_FORMAT_YUV422_TRUNCATED;
			break;
		default:
			break;
		}

		switch (param_set->dma_output_yuv.format) {
		case DMA_OUTPUT_FORMAT_YUV444:
			param_set->dma_output_yuv.format = DMA_OUTPUT_FORMAT_YUV444_TRUNCATED;
			break;
		case DMA_OUTPUT_FORMAT_YUV422:
			param_set->dma_output_yuv.format = DMA_OUTPUT_FORMAT_YUV422_TRUNCATED;
			break;
		default:
			break;
		}
	}

	dbg_hw(2, "[%d][F:%d]%s: OTF[%d]%s(%d), DMA[%d]%s(%d)\n",
		param_set->instance_id, fcount, __func__,
		param_set->otf_output.cmd,
		(param_set->otf_output.format >= OTF_OUTPUT_FORMAT_YUV444_TRUNCATED ? "N" : "W"),
		param_set->otf_output.format,
		param_set->dma_output_yuv.cmd,
		(param_set->dma_output_yuv.format >= DMA_OUTPUT_FORMAT_YUV444_TRUNCATED ? "N" : "W"),
		param_set->dma_output_yuv.format);

	return ret;
}

static void is_hw_isp_update_param(struct is_hw_ip *hw_ip, struct is_region *region,
	struct isp_param_set *param_set, u32 lindex, u32 hindex, u32 instance)
{
	struct isp_param *param;

	FIMC_BUG_VOID(!region);
	FIMC_BUG_VOID(!param_set);

	param = &region->parameter.isp;
	param_set->instance_id = instance;

	/* check input */
	if (lindex & LOWBIT_OF(PARAM_ISP_OTF_INPUT)) {
		memcpy(&param_set->otf_input, &param->otf_input,
			sizeof(struct param_otf_input));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA1_INPUT)) {
		memcpy(&param_set->dma_input, &param->vdma1_input,
			sizeof(struct param_dma_input));
	}

#if defined(SOC_TNR_MERGER)
	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA2_INPUT)) {
		memcpy(&param_set->prev_wgt_dma_input, &param->vdma2_input,
			sizeof(struct param_dma_input));
	}
#endif

	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA3_INPUT)) {
		memcpy(&param_set->prev_dma_input, &param->vdma3_input,
			sizeof(struct param_dma_input));
	}

#if defined(SOC_MCFP)
	if (lindex & LOWBIT_OF(PARAM_ISP_MOTION_DMA_INPUT)) {
		memcpy(&param_set->motion_dma_input, &param->motion_dma_input,
			sizeof(struct param_dma_input));
	}
#endif

#if defined(ENABLE_RGB_REPROCESSING)
	if (lindex & LOWBIT_OF(PARAM_ISP_RGB_INPUT)) {
		memcpy(&param_set->dma_input_rgb, &param->rgb_input,
			sizeof(struct param_dma_input));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_NOISE_INPUT)) {
		memcpy(&param_set->dma_input_noise, &param->noise_input,
			sizeof(struct param_dma_input));
	}
#endif

#if defined(ENABLE_SC_MAP)
	if (lindex & LOWBIT_OF(PARAM_ISP_SCMAP_INPUT)) {
		memcpy(&param_set->dma_input_scmap, &param->scmap_input,
			sizeof(struct param_dma_input));
	}
#endif

	/* check output*/
	if (lindex & LOWBIT_OF(PARAM_ISP_OTF_OUTPUT)) {
		memcpy(&param_set->otf_output, &param->otf_output,
			sizeof(struct param_otf_output));
	}

#if defined(SOC_YPP)
	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA4_OUTPUT)) {
		memcpy(&param_set->dma_output_yuv, &param->vdma4_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA5_OUTPUT)) {
		memcpy(&param_set->dma_output_rgb, &param->vdma5_output,
			sizeof(struct param_dma_output));
	}
#else
	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA4_OUTPUT)) {
		memcpy(&param_set->dma_output_chunk, &param->vdma4_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA5_OUTPUT)) {
		memcpy(&param_set->dma_output_yuv, &param->vdma5_output,
			sizeof(struct param_dma_output));
	}
#endif

#if defined(SOC_TNR_MERGER)
	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA6_OUTPUT)) {
		memcpy(&param_set->dma_output_tnr_prev, &param->vdma6_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_VDMA7_OUTPUT)) {
		memcpy(&param_set->dma_output_tnr_wgt, &param->vdma7_output,
			sizeof(struct param_dma_output));
	}
#endif

#if defined(SOC_YPP)
	if (lindex & LOWBIT_OF(PARAM_ISP_NRDS_OUTPUT)) {
		memcpy(&param_set->dma_output_nrds, &param->nrds_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_NOISE_OUTPUT)) {
		memcpy(&param_set->dma_output_noise, &param->noise_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_DRC_OUTPUT)) {
		memcpy(&param_set->dma_output_drc, &param->drc_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_ISP_HIST_OUTPUT)) {
		memcpy(&param_set->dma_output_hist, &param->hist_output,
			sizeof(struct param_dma_output));
	}
#endif

#if defined(ENABLE_RGB_REPROCESSING)
	if (lindex & LOWBIT_OF(PARAM_ISP_NOISE_REP_OUTPUT)) {
		memcpy(&param_set->dma_output_noise_rep, &param->noise_rep_output,
			sizeof(struct param_dma_output));
	}
#endif

#ifdef CHAIN_USE_STRIPE_PROCESSING
	if ((lindex & LOWBIT_OF(PARAM_ISP_STRIPE_INPUT)) || (hindex & HIGHBIT_OF(PARAM_ISP_STRIPE_INPUT))) {
		memcpy(&param_set->stripe_input, &param->stripe_input,
			sizeof(struct param_stripe_input));
	}
#endif
}

static int is_hw_isp_shot(struct is_hw_ip *hw_ip, struct is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	int i, cur_idx, batch_num;
#if defined(SOC_TNR_MERGER)
	int j;
#endif
	struct is_hw_isp *hw_isp;
	struct isp_param_set *param_set;
	struct is_region *region;
	struct isp_param *param;
	u32 lindex = 0;
	u32 hindex, fcount, instance;
	bool frame_done = false;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	instance = frame->instance;
	msdbgs_hw(2, "[F:%d]shot\n", instance, hw_ip, frame->fcount);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	is_hw_g_ctrl(hw_ip, hw_ip->id, HW_G_CTRL_FRM_DONE_WITH_DMA, (void *)&frame_done);
	if ((!frame_done)
		|| (!test_bit(ENTRY_IXC, &frame->out_flag)
			&& !test_bit(ENTRY_IXP, &frame->out_flag)
			&& !test_bit(ENTRY_IXT, &frame->out_flag)
#if defined(SOC_TNR_MERGER)
			&& !test_bit(ENTRY_IXG, &frame->out_flag)
			&& !test_bit(ENTRY_IXV, &frame->out_flag)
			&& !test_bit(ENTRY_IXW, &frame->out_flag)
#endif
			&& !test_bit(ENTRY_MEXC, &frame->out_flag)))
		set_bit(hw_ip->id, &frame->core_flag);

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;
	param_set = &hw_isp->param_set[instance];
	region = hw_ip->region[instance];
	FIMC_BUG(!region);

	param = &region->parameter.isp;
	fcount = frame->fcount;
	cur_idx = frame->cur_buf_index;

	if (frame->type == SHOT_TYPE_INTERNAL) {
		/* OTF INPUT case */
		param_set->dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
		param_set->prev_dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
		param_set->input_dva[0] = 0x0;
#if defined(SOC_YPP)
		param_set->dma_output_rgb.cmd = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_rgb[0] = 0x0;
#else
		param_set->dma_output_chunk.cmd = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_chunk[0] = 0x0;
#endif
		param_set->dma_output_yuv.cmd  = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_yuv[0] = 0x0;
#if defined(SOC_TNR_MERGER)
		param_set->output_kva_ext[0] = 0x0;
		param_set->prev_wgt_dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
		param_set->input_dva_tnr_wgt[0] = 0x0;
		param_set->dma_output_tnr_wgt.cmd = DMA_INPUT_COMMAND_DISABLE;
		param_set->output_dva_tnr_wgt[0] = 0x0;
		param_set->dma_output_tnr_prev.cmd = DMA_INPUT_COMMAND_DISABLE;
		param_set->output_dva_tnr_prev[0] = 0x0;
		param_set->tnr_mode = TNR_PROCESSING_NONE;
#endif
		hw_ip->internal_fcount[instance] = fcount;
		goto config;
	} else {
		FIMC_BUG(!frame->shot);
		/* per-frame control
		 * check & update size from region
		 */
		lindex = frame->shot->ctl.vendor_entry.lowIndexParam;
		hindex = frame->shot->ctl.vendor_entry.highIndexParam;

		if (hw_ip->internal_fcount[instance] != 0) {
			hw_ip->internal_fcount[instance] = 0;
#if defined(SOC_YPP)
			param_set->dma_output_yuv.cmd = param->vdma4_output.cmd;
			param_set->dma_output_rgb.cmd  = param->vdma5_output.cmd;
#else
			param_set->dma_output_chunk.cmd = param->vdma4_output.cmd;
			param_set->dma_output_yuv.cmd  = param->vdma5_output.cmd;
#endif
		}

		/*set TNR operation mode */
		if (frame->shot_ext) {
			if ((frame->shot_ext->tnr_mode >= TNR_PROCESSING_CAPTURE_FIRST) &&
					!CHK_VIDEOHDR_MODE_CHANGE(param_set->tnr_mode, frame->shot_ext->tnr_mode))
				msinfo_hw("[F:%d] TNR mode is changed (%d -> %d)\n",
					instance, hw_ip, frame->fcount,
					param_set->tnr_mode, frame->shot_ext->tnr_mode);
			param_set->tnr_mode = frame->shot_ext->tnr_mode;
		} else {
			mswarn_hw("frame->shot_ext is null", instance, hw_ip);
			param_set->tnr_mode = TNR_PROCESSING_NORMAL;
		}
	}

	is_hw_isp_update_param(hw_ip, region, param_set, lindex, hindex, instance);

	/* DMA settings */
	if (param_set->dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			param_set->input_dva[i] = (typeof(*param_set->input_dva))
				frame->dvaddr_buffer[i + cur_idx];
			if (param_set->input_dva[i] == 0) {
				msinfo_hw("[F:%d]dvaddr_buffer[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				FIMC_BUG(1);
			}
#if defined(SOC_TNR_MERGER)
			if (frame->ext_planes) {
				for (j = 0; j < frame->ext_planes; j++) {
					param_set->output_kva_ext[j] = frame->kvaddr_ext[j];
					if (frame->kvaddr_ext[j] == 0)
						 msinfo_hw("[F:%d]kvaddr_ext[%d] is zero",
							 instance, hw_ip, frame->fcount, j);
				}
			} else {
				 param_set->output_kva_ext[0] = 0;
			}
#endif
		}
	}

	/* Slave input */
#if defined(SOC_TNR_MERGER)
	/* TNR prev image input */
	if (param_set->prev_dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			param_set->input_dva_tnr_prev[i] = frame->ixtTargetAddress[i + cur_idx];
			if (param_set->input_dva_tnr_prev[i] == 0) {
				msinfo_hw("[F:%d]ixtTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->prev_dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
				FIMC_BUG(1);
			}
		}
	}

	/* TNR prev weight input */
	if (param_set->prev_wgt_dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			param_set->input_dva_tnr_wgt[i] = frame->ixgTargetAddress[i + cur_idx];
			if (param_set->input_dva_tnr_wgt[i] == 0) {
				msinfo_hw("[F:%d]ixgTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->prev_wgt_dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
				FIMC_BUG(1);
			}
		}
	}
#else
	if (param_set->prev_dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			param_set->tnr_prev_input_dva[i] = frame->ixtTargetAddress[i + cur_idx];
			if (param_set->tnr_prev_input_dva[i] == 0) {
				msinfo_hw("[F:%d]ixtTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->prev_dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
				FIMC_BUG(1);
			}
		}
	}
#endif

#if defined(SOC_MCFP)
	if (param_set->motion_dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			/* TODO: set Motion DMA input address */
		}
	}
#endif

#if defined(ENABLE_RGB_REPROCESSING)
	if (param_set->dma_input_rgb.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			/* TODO: RGB DMA input address */
		}
	}

	if (param_set->dma_input_noise.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			/* TODO: Noise DMA input address */
		}
	}
#endif

#if defined(ENABLE_SC_MAP)
	if (param_set->dma_input_scmap.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->num_buffers; i++) {
			/* TODO: SCMAP DMA input address */
		}
	}
#endif

#if defined(SOC_YPP)
	if (param_set->dma_output_yuv.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_yuv.plane; i++) {
			param_set->output_dva_yuv[i] = frame->ixcTargetAddress[i + cur_idx];
			if (param_set->output_dva_yuv[i] == 0) {
				msinfo_hw("[F:%d]ixcTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_yuv.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}

	if (param_set->dma_output_rgb.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_rgb.plane; i++) {
			param_set->output_dva_rgb[i] = frame->ixpTargetAddress[i + cur_idx];
			if (param_set->output_dva_rgb[i] == 0) {
				msinfo_hw("[F:%d]ixpTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_rgb.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}
#else
	if (param_set->dma_output_chunk.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_chunk.plane; i++) {
			param_set->output_dva_chunk[i] = frame->ixpTargetAddress[i + cur_idx];
			if (param_set->output_dva_chunk[i] == 0) {
				msinfo_hw("[F:%d]ixpTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_chunk.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}

	if (param_set->dma_output_yuv.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_yuv.plane; i++) {
			param_set->output_dva_yuv[i] = frame->ixcTargetAddress[i + cur_idx];
			if (param_set->output_dva_yuv[i] == 0) {
				msinfo_hw("[F:%d]ixcTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_yuv.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}
#endif

#if defined(SOC_TNR_MERGER)
	if (param_set->dma_output_tnr_prev.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_tnr_prev.plane; i++) {
			param_set->output_dva_tnr_prev[i] = frame->ixvTargetAddress[i + cur_idx];
			if (param_set->output_dva_tnr_prev[i] == 0) {
				msinfo_hw("[F:%d]ixvTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_tnr_prev.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}

	if (param_set->dma_output_tnr_wgt.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_tnr_wgt.plane; i++) {
			param_set->output_dva_tnr_wgt[i] = frame->ixwTargetAddress[i + cur_idx];
			if (param_set->output_dva_tnr_wgt[i] == 0) {
				msinfo_hw("[F:%d]ixwTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_tnr_wgt.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}
#endif

#if defined(SOC_YPP)
	if (param_set->dma_output_nrds.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_nrds.plane; i++) {
			/* TODO: nrds DMA output address */
		}
	}

	if (param_set->dma_output_noise.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_noise.plane; i++) {
			/* TODO: noise DMA output address */
		}
	}

	if (param_set->dma_output_drc.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_drc.plane; i++) {
			/* TODO: DRC DMA output address */
		}
	}

	if (param_set->dma_output_hist.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_hist.plane; i++) {
			/* TODO: Hist DMA output address */
		}
	}
#endif

#if defined(ENABLE_RGB_REPROCESSING)
	if (param_set->dma_output_noise_rep.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param_set->dma_output_drc.plane; i++) {
			/* TODO: Noise Rep DMA output address */
		}
	}
#endif

config:
	param_set->instance_id = instance;
	param_set->fcount = fcount;

	/* multi-buffer */
	hw_ip->num_buffers = frame->num_buffers;
	batch_num = hw_ip->framemgr->batch_num;
	if (batch_num > 1) {
		hw_ip->num_buffers |= batch_num << SW_FRO_NUM_SHIFT;
		hw_ip->num_buffers |= cur_idx << CURR_INDEX_SHIFT;
	}

	if (frame->type == SHOT_TYPE_INTERNAL) {
		is_log_write("[@][DRV][%d]isp_shot [T:%d][R:%d][F:%d][IN:0x%x] [%d][OUT:0x%x]\n",
			param_set->instance_id, frame->type, param_set->reprocessing,
			param_set->fcount, param_set->input_dva[0],
			param_set->dma_output_yuv.cmd, param_set->output_dva_yuv[0]);
	}

	if (frame->shot) {
		ret = is_lib_isp_set_ctrl(hw_ip, &hw_isp->lib[instance], frame);
		if (ret)
			mserr_hw("set_ctrl fail", instance, hw_ip);
	}

	if (param_set->otf_input.cmd == OTF_INPUT_COMMAND_ENABLE) {
		struct is_hw_ip *hw_ip_3aa = NULL;
		struct is_hw_3aa *hw_3aa = NULL;
		enum is_hardware_id hw_id = DEV_HW_END;
		int hw_slot = 0;

		if (test_bit(DEV_HW_3AA0, &hw_map))
			hw_id = DEV_HW_3AA0;
		else if (test_bit(DEV_HW_3AA1, &hw_map))
			hw_id = DEV_HW_3AA1;
		else if (test_bit(DEV_HW_3AA2, &hw_map))
			hw_id = DEV_HW_3AA2;
		else if (test_bit(DEV_HW_3AA3, &hw_map))
			hw_id = DEV_HW_3AA3;

		hw_slot = is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip_3aa = &hw_ip->hardware->hw_ip[hw_slot];
			FIMC_BUG(!hw_ip_3aa->priv_info);
			hw_3aa = (struct is_hw_3aa *)hw_ip_3aa->priv_info;
			param_set->taa_param = &hw_3aa->param_set[instance];
			/* When the ISP shot is requested, DDK needs to know the size fo 3AA.
			 * This is because DDK calculates the position of the cropped image
			 * from the 3AA size.
			 */
			is_hw_3aa_update_param(hw_ip,
				&region->parameter, param_set->taa_param,
				lindex, hindex, instance);
		}
	}

	ret = is_hw_isp_set_yuv_range(hw_ip, param_set, frame->fcount, hw_map);
	ret |= is_lib_isp_shot(hw_ip, &hw_isp->lib[instance], param_set, frame->shot);

	set_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

static int is_hw_isp_set_param(struct is_hw_ip *hw_ip, struct is_region *region,
	u32 lindex, u32 hindex, u32 instance, ulong hw_map)
{
	int ret = 0;
	struct is_hw_isp *hw_isp;
	struct isp_param_set *param_set;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;
	param_set = &hw_isp->param_set[instance];

	hw_ip->region[instance] = region;
	hw_ip->lindex[instance] = lindex;
	hw_ip->hindex[instance] = hindex;

	is_hw_isp_update_param(hw_ip, region, param_set, lindex, hindex, instance);

	return ret;
}

static int is_hw_isp_get_meta(struct is_hw_ip *hw_ip, struct is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	struct is_hw_isp *hw_isp;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	ret = is_lib_isp_get_meta(hw_ip, &hw_isp->lib[frame->instance], frame);
	if (ret)
		mserr_hw("get_meta fail", frame->instance, hw_ip);

	if (frame->shot) {
		msdbg_hw(2, "%s: [F:%d], %d,%d,%d\n", frame->instance, hw_ip, __func__,
			frame->fcount,
			frame->shot->udm.ni.currentFrameNoiseIndex,
			frame->shot->udm.ni.nextFrameNoiseIndex,
			frame->shot->udm.ni.nextNextFrameNoiseIndex);
	}

	return ret;
}

static int is_hw_isp_frame_ndone(struct is_hw_ip *hw_ip, struct is_frame *frame,
	u32 instance, enum ShotErrorType done_type)
{
	int output_id = 0;
	int ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	if (test_bit(hw_ip->id, &frame->core_flag))
		output_id = IS_HW_CORE_END;

	ret = is_hardware_frame_done(hw_ip, frame, -1,
			output_id, done_type, false);

	return ret;
}

static int is_hw_isp_load_setfile(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int flag = 0, ret = 0;
	ulong addr;
	u32 size, index;
	struct is_hw_isp *hw_isp = NULL;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (test_bit(DEV_HW_3AA0, &hw_map) ||
		test_bit(DEV_HW_3AA1, &hw_map) ||
		test_bit(DEV_HW_3AA2, &hw_map))
		return 0;

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -ESRCH;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	switch (setfile->version) {
	case SETFILE_V2:
		flag = false;
		break;
	case SETFILE_V3:
		flag = true;
		break;
	default:
		mserr_hw("invalid version (%d)", instance, hw_ip,
			setfile->version);
		return -EINVAL;
	}

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	for (index = 0; index < setfile->using_count; index++) {
		addr = setfile->table[index].addr;
		size = setfile->table[index].size;
		ret = is_lib_isp_create_tune_set(&hw_isp->lib[instance],
			addr, size, index, flag, instance);

		set_bit(index, &hw_isp->lib[instance].tune_count);
	}

	set_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

static int is_hw_isp_apply_setfile(struct is_hw_ip *hw_ip, u32 scenario,
	u32 instance, ulong hw_map)
{
	int ret = 0;
	u32 setfile_index = 0;
	struct is_hw_isp *hw_isp = NULL;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (test_bit(DEV_HW_3AA0, &hw_map) ||
		test_bit(DEV_HW_3AA1, &hw_map) ||
		test_bit(DEV_HW_3AA2, &hw_map))
		return 0;

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -ESRCH;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	if (setfile->using_count == 0)
		return 0;

	setfile_index = setfile->index[scenario];
	if (setfile_index >= setfile->using_count) {
		mserr_hw("setfile index is out-of-range, [%d:%d]",
				instance, hw_ip, scenario, setfile_index);
		return -EINVAL;
	}

	msinfo_hw("setfile (%d) scenario (%d)\n", instance, hw_ip,
		setfile_index, scenario);

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	ret = is_lib_isp_apply_tune_set(&hw_isp->lib[instance], setfile_index, instance);

	return ret;
}

static int is_hw_isp_delete_setfile(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	struct is_hw_isp *hw_isp = NULL;
	int i, ret = 0;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (test_bit(DEV_HW_3AA0, &hw_map) ||
		test_bit(DEV_HW_3AA1, &hw_map) ||
		test_bit(DEV_HW_3AA2, &hw_map))
		return 0;

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		msdbg_hw(2, "Not initialized\n", instance, hw_ip);
		return 0;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	if (setfile->using_count == 0)
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	for (i = 0; i < setfile->using_count; i++) {
		if (test_bit(i, &hw_isp->lib[instance].tune_count)) {
			ret = is_lib_isp_delete_tune_set(&hw_isp->lib[instance],
				(u32)i, instance);
			clear_bit(i, &hw_isp->lib[instance].tune_count);
		}
	}

	clear_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

int is_hw_isp_restore(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_isp *hw_isp = NULL;

	BUG_ON(!hw_ip);
	BUG_ON(!hw_ip->priv_info);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return -EINVAL;

	hw_isp = (struct is_hw_isp *)hw_ip->priv_info;

	ret = is_lib_isp_reset_recovery(hw_ip, &hw_isp->lib[instance], instance);
	if (ret) {
		mserr_hw("is_lib_isp_reset_recovery fail ret(%d)",
				instance, hw_ip, ret);
	}

	return ret;
}

const struct is_hw_ip_ops is_hw_isp_ops = {
	.open			= is_hw_isp_open,
	.init			= is_hw_isp_init,
	.deinit			= is_hw_isp_deinit,
	.close			= is_hw_isp_close,
	.enable			= is_hw_isp_enable,
	.disable		= is_hw_isp_disable,
	.shot			= is_hw_isp_shot,
	.set_param		= is_hw_isp_set_param,
	.get_meta		= is_hw_isp_get_meta,
	.frame_ndone		= is_hw_isp_frame_ndone,
	.load_setfile		= is_hw_isp_load_setfile,
	.apply_setfile		= is_hw_isp_apply_setfile,
	.delete_setfile		= is_hw_isp_delete_setfile,
	.restore		= is_hw_isp_restore
};

int is_hw_isp_probe(struct is_hw_ip *hw_ip, struct is_interface *itf,
	struct is_interface_ischain *itfc, int id, const char *name)
{
	int ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!itf);
	FIMC_BUG(!itfc);

	/* initialize device hardware */
	hw_ip->id   = id;
	snprintf(hw_ip->name, sizeof(hw_ip->name), "%s", name);
	hw_ip->ops  = &is_hw_isp_ops;
	hw_ip->itf  = itf;
	hw_ip->itfc = itfc;
	atomic_set(&hw_ip->fcount, 0);
	hw_ip->is_leader = true;
	atomic_set(&hw_ip->status.Vvalid, V_BLANK);
	atomic_set(&hw_ip->rsccount, 0);
	atomic_set(&hw_ip->run_rsccount, 0);
	init_waitqueue_head(&hw_ip->status.wait_queue);

	clear_bit(HW_OPEN, &hw_ip->state);
	clear_bit(HW_INIT, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);
	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_TUNESET, &hw_ip->state);

	sinfo_hw("probe done\n", hw_ip);

	return ret;
}
