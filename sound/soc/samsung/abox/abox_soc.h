/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ALSA SoC - Samsung Abox SoC layer
 *
 * Copyright (c) 2018 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_ABOX_SOC_H
#define __SND_SOC_ABOX_SOC_H

#include "abox.h"

#define BITS_PER_SFR	32
#define SFR_STRIDE	4

#define GENMASK32(h, l) \
	(((~0U) - (1U << (l)) + 1) & (~0U >> (32 - 1 - (h))))
#define ABOX_SOC_VERSION(m, n, r) (((m) << 16) | ((n) << 8) | (r))
#define ABOX_FLD(name) (GENMASK32(ABOX_##name##_H, ABOX_##name##_L))
#define ABOX_FLD_X(name, x) (GENMASK32(ABOX_##name##_H(x), ABOX_##name##_L(x)))
#define ABOX_FLD_PER_SFR(name) (BITS_PER_SFR / ABOX_##name##_ITV)
#define ABOX_SFR(name, o, x) \
	(ABOX_##name##_BASE + ((x) * ABOX_##name##_ITV) + (o))
#define ABOX_FLD_OFFSET(name, o, x) \
	((((x) + (o)) / ABOX_FLD_PER_SFR(name)) * SFR_STRIDE)
#define ABOX_SFR_FLD(name, fld, fo, fx) \
	(ABOX_##name + ABOX_FLD_OFFSET(fld, fo, fx))
#define ABOX_SFR_FLD_X(name, x, fld, fo, fx) \
	(ABOX_##name(x) + ABOX_FLD_OFFSET(fld, fo, fx))
#define ABOX_L(name, o, x) (ABOX_SFR(name, o, x) % BITS_PER_SFR)
#define ABOX_H(name, o, x) (ABOX_SFR(name, o, x) % BITS_PER_SFR)

#if (ABOX_SOC_VERSION(4, 0x20, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_42.h"
#elif (ABOX_SOC_VERSION(4, 0, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_4.h"
#elif (ABOX_SOC_VERSION(3, 1, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_31.h"
#elif (ABOX_SOC_VERSION(3, 0, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_3.h"
#elif (ABOX_SOC_VERSION(2, 3, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_23.h"
#elif (ABOX_SOC_VERSION(2, 1, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_21.h"
#else
#include "abox_soc_2.h"
#endif

#define set_mask_value(id, mask, value) \
		{id = (typeof(id))((id & ~mask) | (value & mask)); }

#define set_value_by_name(id, name, value) \
		set_mask_value(id, name##_MASK, value << name##_L)

/**
 * get version of ABOX IP
 * @param[in]	adev		pointer to abox device
 * @return	ABOX IP version. Format: major << 16 | minor << 8 | revision
 */
extern int abox_soc_ver(struct device *adev);

/**
 * compare version of ABOX IP
 * @param[in]	adev		pointer to abox device
 * @param[in]	m		major version
 * @param[in]	n		minor version
 * @param[in]	r		revision
 * @return	> 0: given version is bigger than SoC version
 *		= 0: given version is same with SoC version
 *		< 0: given version is smaller with SoC version
 */
extern int abox_soc_vercmp(struct device *adev, int m, int n, int r);

/**
 * register sfr regmap
 * @param[in]	adev		pointer to abox device
 * @return	regmap for sfr
 */
extern struct regmap *abox_soc_get_regmap(struct device *adev);

/****** ABOX SoC internal API ******/

/**
 * Check whether a register is accessible or not
 * @param[in]	reg	register address
 * @return	true or false
 */
extern bool accessible_reg(unsigned int reg);

/**
 * Check whether a register is read only or not
 * @param[in]	reg	register address
 * @return	true or false
 */
extern bool readonly_reg(unsigned int reg);

/**
 * Check whether a register is write only or not
 * @param[in]	reg	register address
 * @return	true or false
 */
extern bool writeonly_reg(unsigned int reg);

/**
 * Check whether a register is shared with firmware or not
 * @param[in]	reg	register address
 * @return	true or false
 */
extern bool shared_reg(unsigned int reg);

/**
 * Apply register default values
 * @param[in]	regmap	regmap
 * @return	0 or error code
 */
extern int apply_patch(struct regmap *regmap);

/****** ABOX SoC internal API ******/

#endif /* __SND_SOC_ABOX_SOC_H */
