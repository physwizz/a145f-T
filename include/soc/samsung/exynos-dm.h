/* linux/include/soc/samsung/exynos-dm.h
 *
 * Copyright (C) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 - Header file for exynos DVFS Manager support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_DM_H
#define __EXYNOS_DM_H

#include <linux/irq_work.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/types.h>

#define EXYNOS_DM_MODULE_NAME		"exynos-dm"
#define EXYNOS_DM_TYPE_NAME_LEN		16
#define EXYNOS_DM_ATTR_NAME_LEN		(EXYNOS_DM_TYPE_NAME_LEN + 12)

#define EXYNOS_DM_RELATION_L		0
#define EXYNOS_DM_RELATION_H		1

enum exynos_dm_type {
	DM_CPU_CL0 = 0,
	DM_CPU_CL1,
	DM_MIF,
	DM_INT,
	DM_CAM,
	DM_DISP,
	DM_AUD,
	DM_GPU,
	DM_TYPE_END
};

enum exynos_constraint_type {
	CONSTRAINT_MIN = 0,
	CONSTRAINT_MAX,
	CONSTRAINT_END
};

enum dvfs_direction {
	DOWN = 0,
	UP,
	DIRECTION_END
};

struct exynos_dm_freq {
	u32				master_freq;
	u32				slave_freq;
};

struct exynos_dm_attrs {
	struct device_attribute attr;
	char name[EXYNOS_DM_ATTR_NAME_LEN];
};

struct exynos_dm_constraint {
	int					dm_master;
	int					dm_slave;

	struct list_head	master_domain;
	struct list_head	slave_domain;

	bool				guidance;		/* check constraint table by hw guide */
	bool				support_dynamic_disable;
	u32				table_length;

	enum exynos_constraint_type	constraint_type;
	char				dm_type_name[EXYNOS_DM_TYPE_NAME_LEN];
	struct exynos_dm_freq		*freq_table;
	struct exynos_dm_freq		**variable_freq_table;
	bool				support_variable_freq_table;
	u32				num_table_index;
	u32					const_freq;
	u32					gov_freq;

	struct exynos_dm_constraint	*sub_constraint;
};

struct exynos_dm_data {
	bool				available;		/* use for DVFS domain available */
#if defined(CONFIG_EXYNOS_ACPM) || defined(CONFIG_EXYNOS_ACPM_MODULE)
	bool				policy_use;
#endif
	int		dm_type;
	char				dm_type_name[EXYNOS_DM_TYPE_NAME_LEN];

	int			my_order;		// Scaling order in domain_order
	int			indegree;		// Number of min masters

	u32			cur_freq;		// Current frequency
	u32			next_target_freq;	// Next target frequency determined by current status
	u32			governor_freq;	// Frequency determined by DVFS governor
	u32			gov_min;		// Constraint by current frequency of min master domains
	u32			policy_min;		// Min frequency limition in this domin
	u32			policy_max;		// Min frequency limition in this domin
	u32			const_min;		// Constraint by min frequency of min master domains
	u32			const_max;		// Constraint1 by max frequency of max master domains

	int				(*freq_scaler)(int dm_type, void *devdata, u32 target_freq, unsigned int relation);

	struct list_head		min_slaves;
	struct list_head		max_slaves;
	struct list_head		min_masters;
	struct list_head		max_masters;

#if defined(CONFIG_EXYNOS_ACPM) || defined(CONFIG_EXYNOS_ACPM_MODULE)
	u32				cal_id;
#endif

	void				*devdata;

	struct exynos_dm_attrs		dm_policy_attr;
	struct exynos_dm_attrs		constraint_table_attr;
};

struct exynos_dm_device {
	struct device			*dev;
	struct mutex			lock;
	int				domain_count;
	int 			constraint_domain_count;
	int				*domain_order;
	struct exynos_dm_data		*dm_data;
	int				dynamic_disable;
};

/* External Function call */
#if defined(CONFIG_EXYNOS_DVFS_MANAGER) || defined(CONFIG_EXYNOS_DVFS_MANAGER_MODULE)
extern int exynos_dm_data_init(int dm_type, void *data,
			u32 min_freq, u32 max_freq, u32 cur_freq);
extern int register_exynos_dm_constraint_table(int dm_type,
				struct exynos_dm_constraint *constraint);
extern int unregister_exynos_dm_constraint_table(int dm_type,
				struct exynos_dm_constraint *constraint);
extern int register_exynos_dm_freq_scaler(int dm_type,
			int (*scaler_func)(int dm_type, void *devdata, u32 target_freq, unsigned int relation));
extern int unregister_exynos_dm_freq_scaler(int dm_type);
extern int policy_update_call_to_DM(int dm_type, u32 min_freq, u32 max_freq);
extern int DM_CALL(int dm_type, unsigned long *target_freq);
extern void exynos_dm_dynamic_disable(int flag);
extern int exynos_dm_change_freq_table(struct exynos_dm_constraint *constraint, int idx);
#else
static inline
int exynos_dm_data_init(int dm_type, void *data,
			u32 min_freq, u32 max_freq, u32 cur_freq)
{
	return -ENODEV;
}
static inline
int register_exynos_dm_constraint_table(int dm_type,
				struct exynos_dm_constraint *constraint)
{
	return -ENODEV;
}
static inline
int unregister_exynos_dm_constraint_table(int dm_type,
				struct exynos_dm_constraint *constraint)
{
	return -ENODEV;
}
static inline
int register_exynos_dm_freq_scaler(int dm_type,
			int (*scaler_func)(int dm_type, void *devdata, u32 target_freq, unsigned int relation))
{
	return -ENODEV;
}
static inline
int unregister_exynos_dm_freq_scaler(int dm_type)
{
	return -ENODEV;
}
static inline
int policy_update_call_to_DM(int dm_type, u32 min_freq, u32 max_freq)
{
	return -ENODEV;
}
static inline
int DM_CALL(int dm_type, unsigned long *target_freq)
{
	return -ENODEV;
}
static inline
void exynos_dm_dynamic_disable(int flag)
{
	return;
}
static inline int exynos_dm_change_freq_table(struct exynos_dm_constraint *constraint, int idx)
{
	return -ENODEV;
}
#endif

#endif /* __EXYNOS_DM_H */
