#ifndef __FT8720_A14_PANEL_I2C_H__
#define __FT8720_A14_PANEL_I2C_H__

#include "../panel_drv.h"

struct i2c_data ft8720_skyworth_a14_i2c_data = {
	.vendor = "TI",
	.model = "LM36274",
	.addr_len = 0x01,
	.data_len = 0x01,
};

#endif
