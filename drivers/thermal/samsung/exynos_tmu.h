/*
 * exynos_tmu.h - Samsung EXYNOS TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _EXYNOS_TMU_H
#define _EXYNOS_TMU_H
#include <soc/samsung/cpu_cooling.h>
#include <soc/samsung/gpu_cooling.h>
#include <soc/samsung/isp_cooling.h>
#include <soc/samsung/exynos_pm_qos.h>
#include <soc/samsung/exynos-cpuhp.h>
#include <linux/kthread.h>
#include <dt-bindings/thermal/thermal_exynos.h>

#define MCELSIUS        1000

enum soc_type {
	SOC_ARCH_EXYNOS3830 = 1,
};
struct exynos_pi_param {
	s64 err_integral;
	int trip_switch_on;
	int trip_control_temp;

	u32 sustainable_power;
	s32 k_po;
	s32 k_pu;
	s32 k_i;
	s32 i_max;
	s32 integral_cutoff;

	int polling_delay_on;
	int polling_delay_off;

	bool switched_on;
};

/**
 * struct exynos_tmu_platform_data
 * @gain: gain of amplifier in the positive-TC generator block
 *	0 < gain <= 15
 * @reference_voltage: reference voltage of amplifier
 *	in the positive-TC generator block
 *	0 < reference_voltage <= 31
 * @noise_cancel_mode: noise cancellation mode
 *	000, 100, 101, 110 and 111 can be different modes
 * @type: determines the type of SOC
 * @default_temp_offset: default temperature offset in case of no trimming
 * @cal_type: calibration type for temperature
 *
 * This structure is required for configuration of exynos_tmu driver.
 */
struct exynos_tmu_platform_data {
	u8 gain;
	u8 reference_voltage;
	u8 noise_cancel_mode;

	u8 first_point_trim;
	u8 second_point_trim;
	u8 default_temp_offset;
	u32 trip_temp;

	enum soc_type type;
	u32 sensor_type;
	u32 cal_type;
};


struct sensor_info {
	u16 sensor_num;
	u16 cal_type;
	u16 temp_error1;
	u16 temp_error2;
};

/**
 * struct exynos_tmu_data : A structure to hold the private data of the TMU
	driver
 * @id: identifier of the one instance of the TMU controller.
 * @base: base address of the single instance of the TMU controller.
 * @irq: irq number of the TMU controller.
 * @soc: id of the SOC type.
 * @irq_work: pointer to the irq work structure.
 * @lock: lock to implement synchronization.
 * @regulator: pointer to the TMU regulator structure.
 * @reg_conf: pointer to structure to register with core thermal.
 * @ntrip: number of supported trip points.
 * @tmu_initialize: SoC specific TMU initialization method
 * @tmu_control: SoC specific TMU control method
 * @tmu_read: SoC specific TMU temperature read method
 * @tmu_set_emulation: SoC specific TMU emulation setting method
 * @tmu_clear_irqs: SoC specific TMU interrupts clearing method
 */
struct exynos_tmu_data {
	int id;
	/* Throttle hotplug related variables */
	bool hotplug_enable;
	int hotplug_in_threshold;
	int hotplug_out_threshold;
	int limited_frequency;
	struct exynos_tmu_platform_data *pdata;
	void __iomem *base;
	int irq;
	enum soc_type soc;
	struct kthread_worker thermal_worker;
	struct kthread_work irq_work;
	struct kthread_work hotplug_work;
	struct mutex lock;
	u16 temp_error1, temp_error2;
	struct thermal_zone_device *tzd;
	unsigned int ntrip;
	bool enabled;
	struct thermal_cooling_device *cool_dev;
	struct list_head node;
	u32 sensors;
	int num_probe;
	int num_of_sensors;
	struct sensor_info *sensor_info;
	char tmu_name[THERMAL_NAME_LENGTH + 1];
	struct device_node *np;
	struct cpumask cpu_domain;
	bool is_cpu_hotplugged_out;
	char cpuhp_name[THERMAL_NAME_LENGTH + 1];
	int temperature;
	bool use_pi_thermal;
	struct kthread_delayed_work pi_work;
	struct exynos_pi_param *pi_param;
	struct notifier_block nb;
	atomic_t in_suspend;

	bool first_boot;
	unsigned int buf_vref;
	unsigned int buf_slope;
	unsigned int avg_mode;

	ktime_t last_thermal_status_updated;
	ktime_t thermal_status[3];

	int (*tmu_initialize)(struct platform_device *pdev);
	void (*tmu_control)(struct platform_device *pdev, bool on);
	int (*tmu_read)(struct exynos_tmu_data *data);
	void (*tmu_set_emulation)(struct exynos_tmu_data *data, int temp);
	void (*tmu_clear_irqs)(struct exynos_tmu_data *data);
};

extern int exynos_build_static_power_table(struct device_node *np, int **var_table,
		unsigned int *var_volt_size, unsigned int *var_temp_size, char *tz_name);

#if IS_ENABLED(CONFIG_EXYNOS_ACPM_THERMAL)
#if defined(CONFIG_SOC_EXYNOS991) || defined(CONFIG_SOC_EXYNOS2100)
#define PMUREG_AUD_STATUS           0x1884
#define PMUREG_AUD_STATUS_MASK          0x1
#endif
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_ABOX)
extern bool abox_is_on(void);
#else
static inline bool abox_is_on(void)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_ISP_THERMAL)
int exynos_isp_cooling_init(void);
#else
static inline int exynos_isp_cooling_init(void)
{
	return 0;
}
#endif

#endif /* _EXYNOS_TMU_H */
