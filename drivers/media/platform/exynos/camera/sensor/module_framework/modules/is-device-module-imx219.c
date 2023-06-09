/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <exynos-is-sensor.h>
#include "is-hw.h"
#include "is-core.h"
#include "is-device-sensor.h"
#include "is-device-sensor-peri.h"
#include "is-resourcemgr.h"
#include "is-dt.h"

#include "is-device-module-base.h"

static struct is_sensor_cfg config_module_imx219[] = {
	/* 3280x2458@30fps */
	IS_SENSOR_CFG(3280, 2458, 30, 15, 0, CSI_DATA_LANES_4),
	/* 3280x1846@30fps */
	IS_SENSOR_CFG(3280, 1846, 30, 15, 1, CSI_DATA_LANES_4),
	/* 1640x924@60fps */
	IS_SENSOR_CFG(1640, 924, 60, 15, 2, CSI_DATA_LANES_4),
	/* 1640x1232@60fps */
	IS_SENSOR_CFG(1640, 1232, 60, 15, 3, CSI_DATA_LANES_4),
	/* 816x604@120fps */
	IS_SENSOR_CFG(816, 604, 120, 15, 4, CSI_DATA_LANES_4),
	/* 816x460@120fps */
	IS_SENSOR_CFG(816, 460, 120, 15, 5, CSI_DATA_LANES_4),
};

static struct is_vci vci_module_imx219[] = {
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR10,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}, {
		.pixelformat = V4L2_PIX_FMT_SBGGR12,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}, {
		.pixelformat = V4L2_PIX_FMT_SBGGR16,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}
};

static const struct v4l2_subdev_core_ops core_ops = {
	.init = sensor_module_init,
	.g_ctrl = sensor_module_g_ctrl,
	.s_ctrl = sensor_module_s_ctrl,
	.g_ext_ctrls = sensor_module_g_ext_ctrls,
	.s_ext_ctrls = sensor_module_s_ext_ctrls,
	.ioctl = sensor_module_ioctl,
	.log_status = sensor_module_log_status,
};

static const struct v4l2_subdev_video_ops video_ops = {
	.s_routing = sensor_module_s_routing,
	.s_stream = sensor_module_s_stream,
	.s_mbus_fmt = sensor_module_s_format,
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops,
	.video = &video_ops,
};

static int sensor_module_imx219_power_setpin(struct device *dev,
	struct exynos_platform_is_module *pdata)
{
	struct device_node *dnode = dev->of_node;
	int gpio_reset = 0;
	int gpio_mclk = 0;
	int gpio_none = 0;
#if defined (VDDD_1P2_VTCAM_GPIO_CONTROL)
	int gpio_core_en = 0;
#endif
#if defined (VDD_VTCAM_SENSOR_A2P8_GPIO_CONTROL)
	int gpio_cam_avdd_en = 0;
#endif
#if defined (VDD_VTCAM_IO_1P8_GPIO_CONTROL)
	int gpio_cam_io_en = 0;
#endif

	dev_info(dev, "%s E v4\n", __func__);

	/* TODO */
	gpio_reset = of_get_named_gpio(dnode, "gpio_reset", 0);
	if (!gpio_is_valid(gpio_reset)) {
		dev_err(dev, "failed to get PIN_RESET\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_reset);
	}

#if defined (VDDD_1P2_VTCAM_GPIO_CONTROL)
	gpio_core_en = of_get_named_gpio(dnode, "gpio_core_en", 0);
	if (!gpio_is_valid(gpio_core_en)) {
		dev_err(dev, "failed to get gpio_core_en\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_core_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_core_en);
	}
#endif

	gpio_mclk = of_get_named_gpio(dnode, "gpio_mclk", 0);
	if (!gpio_is_valid(gpio_mclk)) {
		dev_err(dev, "failed to get gpio_mclk\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_mclk, GPIOF_OUT_INIT_LOW, "CAM_MCLK_OUTPUT_LOW");
		gpio_free(gpio_mclk);
	}

#if defined (VDD_VTCAM_SENSOR_A2P8_GPIO_CONTROL)
	gpio_cam_avdd_en = of_get_named_gpio(dnode, "gpio_cam_avdd_en", 0);
	if (!gpio_is_valid(gpio_cam_avdd_en)) {
		err("%s failed to get gpio_cam_avdd_en\n",__func__);
		return -EINVAL;
	} else {
		gpio_request_one(gpio_cam_avdd_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_cam_avdd_en);
	}
#endif

#if defined (VDD_VTCAM_IO_1P8_GPIO_CONTROL)
	gpio_cam_io_en = of_get_named_gpio(dnode, "gpio_cam_io_en", 0);
	if (!gpio_is_valid(gpio_cam_io_en)) {
		err("%s failed to get gpio_cam_io_en\n",__func__);
		return -EINVAL;
	} else {
		gpio_request_one(gpio_cam_io_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_cam_io_en);
	}
#endif

	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF);

	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);

	/* FRONT CAEMRA - POWER ON */
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 0);
#if defined (VDD_VTCAM_SENSOR_A2P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_cam_avdd_en, "gpio_cam_avdd_en", PIN_OUTPUT, 1, 0);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_2P8_A", PIN_REGULATOR, 1, 0);
#endif
#if defined (VDDD_1P2_VTCAM_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_core_en, "gpio_core_en", PIN_OUTPUT, 1, 0);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "VDD_VTCAM_CORE_1P2", PIN_REGULATOR, 1, 0);
#endif
#if defined (VDD_VTCAM_IO_1P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_cam_io_en, "gpio_cam_io_en", PIN_OUTPUT, 1, 0);
#else
 	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 1, 0);
#endif
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 2, 1);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst high", PIN_OUTPUT, 1, 0);

	/* FRONT CAEMRA - POWER OFF */
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 1, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_reset, "sen_rst", PIN_OUTPUT, 0, 0);
#if defined (VDD_VTCAM_IO_1P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_cam_io_en, "gpio_cam_io_en", PIN_OUTPUT, 0, 0);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 0, 0);
#endif
#if defined (VDDD_1P2_VTCAM_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_core_en, "gpio_core_en", PIN_OUTPUT, 0, 0);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "VDD_VTCAM_CORE_1P2", PIN_REGULATOR, 0, 0);
#endif
#if defined (VDD_VTCAM_SENSOR_A2P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_cam_avdd_en, "gpio_cam_avdd_en", PIN_OUTPUT, 0, 0);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_2P8_A", PIN_REGULATOR, 0, 0);
#endif

	/* READ_ROM - POWER ON */
#if defined (VDD_VTCAM_SENSOR_A2P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_cam_avdd_en, "gpio_cam_avdd_en", PIN_OUTPUT, 1, 2000);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_2P8_A", PIN_REGULATOR, 1, 2000);
#endif
#if defined (VDD_VTCAM_IO_1P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_cam_io_en, "gpio_cam_io_en", PIN_OUTPUT, 1, 2000);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 1, 2000);
#endif

	/* READ_ROM - POWER OFF */
#if defined (VDD_VTCAM_SENSOR_A2P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_cam_avdd_en, "gpio_cam_avdd_en", PIN_OUTPUT, 0, 10);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_2P8_A", PIN_REGULATOR, 0, 10);
#endif
#if defined (VDD_VTCAM_IO_1P8_GPIO_CONTROL)
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_cam_io_en, "gpio_cam_io_en", PIN_OUTPUT, 0, 0);
#else
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 0, 0);
#endif

	dev_info(dev, "%s X v4\n", __func__);

	return 0;
}

static int __init sensor_module_imx219_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct is_core *core;
	struct v4l2_subdev *subdev_module;
	struct is_module_enum *module;
	struct is_device_sensor *device;
	struct sensor_open_extended *ext;
	struct exynos_platform_is_module *pdata;
	struct device *dev;

	FIMC_BUG(!is_dev);

	core = (struct is_core *)dev_get_drvdata(is_dev);
	if (!core) {
		probe_info("core device is not yet probed");
		return -EPROBE_DEFER;
	}

	dev = &pdev->dev;

	is_module_parse_dt(dev, sensor_module_imx219_power_setpin);

	pdata = dev_get_platdata(dev);
	device = &core->sensor[pdata->id];

	subdev_module = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_module) {
		probe_err("subdev_module is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	probe_info("%s pdta->id(%d), module_enum id = %d \n", __func__, pdata->id, atomic_read(&device->module_count));
	module = &device->module_enum[atomic_read(&device->module_count)];
	atomic_inc(&device->module_count);
	clear_bit(IS_MODULE_GPIO_ON, &module->state);
	module->pdata = pdata;
	module->dev = dev;
	module->sensor_id = SENSOR_NAME_IMX219;
	module->subdev = subdev_module;
	module->device = pdata->id;
	module->client = NULL;
	module->active_width = 3264 + 16;
	module->active_height = 2448 + 10;
	module->margin_left = 0;
	module->margin_right = 0;
	module->margin_top = 0;
	module->margin_bottom = 0;
	module->pixel_width = module->active_width;
	module->pixel_height = module->active_height;
	module->max_framerate = 120;
	module->position = pdata->position;
	module->mode = CSI_MODE_CH0_ONLY;
	module->lanes = CSI_DATA_LANES_4;
	module->bitwidth = 10;
	module->vcis = ARRAY_SIZE(vci_module_imx219);
	module->vci = vci_module_imx219;
	module->sensor_maker = "SONY";
	module->sensor_name = "IMX219";
	module->setfile_name = "setfile_imx219.bin";
	module->cfgs = ARRAY_SIZE(config_module_imx219);
	module->cfg = config_module_imx219;
	module->ops = NULL;
	/* Sensor peri */
	module->private_data = kzalloc(sizeof(struct is_device_sensor_peri), GFP_KERNEL);
	if (!module->private_data) {
		probe_err("is_device_sensor_peri is NULL");
		ret = -ENOMEM;
		goto p_err;
	}
	is_sensor_peri_probe((struct is_device_sensor_peri*)module->private_data);
	PERI_SET_MODULE(module);

	ext = &module->ext;
	ext->mipi_lane_num = module->lanes;
	ext->I2CSclk = 0;

	ext->sensor_con.product_name = module->sensor_id;
	ext->sensor_con.peri_type = SE_I2C;
	ext->sensor_con.peri_setting.i2c.channel = pdata->sensor_i2c_ch;
	ext->sensor_con.peri_setting.i2c.slave_address = pdata->sensor_i2c_addr;
	ext->sensor_con.peri_setting.i2c.speed = 400000;

	if (pdata->af_product_name !=  ACTUATOR_NAME_NOTHING) {
		ext->actuator_con.product_name = pdata->af_product_name;
		ext->actuator_con.peri_type = SE_I2C;
		ext->actuator_con.peri_setting.i2c.channel = pdata->af_i2c_ch;
		ext->actuator_con.peri_setting.i2c.slave_address = pdata->af_i2c_addr;
		ext->actuator_con.peri_setting.i2c.speed = 400000;
	}

	if (pdata->flash_product_name != FLADRV_NAME_NOTHING) {
		ext->flash_con.product_name = pdata->flash_product_name;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = pdata->flash_first_gpio;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = pdata->flash_second_gpio;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	if (pdata->ois_product_name != OIS_NAME_NOTHING) {
		ext->ois_con.product_name = pdata->ois_product_name;
		ext->ois_con.peri_type = SE_I2C;
		ext->ois_con.peri_setting.i2c.channel = pdata->ois_i2c_ch;
		ext->ois_con.peri_setting.i2c.slave_address = pdata->ois_i2c_addr;
		ext->ois_con.peri_setting.i2c.speed = 400000;
	} else {
		ext->ois_con.product_name = pdata->ois_product_name;
		ext->ois_con.peri_type = SE_NULL;
	}

	v4l2_subdev_init(subdev_module, &subdev_ops);

	v4l2_set_subdevdata(subdev_module, module);
	v4l2_set_subdev_hostdata(subdev_module, device);
	snprintf(subdev_module->name, V4L2_SUBDEV_NAME_SIZE, "sensor-subdev.%d", module->sensor_id);

	probe_info("%s done\n", __func__);

p_err:
	return ret;
}

static const struct of_device_id exynos_is_sensor_module_imx219_match[] = {
	{
		.compatible = "samsung,sensor-module-imx219",
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_is_sensor_module_imx219_match);

static struct platform_driver sensor_module_imx219_driver = {
	.driver = {
		.name   = "FIMC-IS-SENSOR-MODULE-IMX219",
		.owner  = THIS_MODULE,
		.of_match_table = exynos_is_sensor_module_imx219_match,
	}
};

static int __init is_sensor_module_imx219_init(void)
{
	int ret;

	ret = platform_driver_probe(&sensor_module_imx219_driver,
				sensor_module_imx219_probe);
	if (ret)
		err("failed to probe %s driver: %d\n",
			sensor_module_imx219_driver.driver.name, ret);

	return ret;
}
late_initcall(is_sensor_module_imx219_init);
