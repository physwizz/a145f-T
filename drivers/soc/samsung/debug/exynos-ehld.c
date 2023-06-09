/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * Exynos - Early Hard Lockup Detector
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/suspend.h>
#include <linux/perf_event.h>
#include <linux/of.h>
#include <linux/cpu_pm.h>
#include <linux/sched/clock.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>
#include <linux/smpboot.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <soc/samsung/exynos-pmu.h>
#include <soc/samsung/exynos-ehld.h>
#include <soc/samsung/debug-snapshot.h>
#include "coresight_regs.h"

//#define DEBUG
#define EHLD_TASK_SUPPORT

#ifdef DEBUG
#define ehld_printk(f, str...) printk(str)
#else
#define ehld_printk(f, str...) if (f == 1) printk(str)
#endif

#define MSB_MASKING		(0x0000FF0000000000)
#define MSB_PADDING		(0xFFFFFF0000000000)
#define DBG_UNLOCK(base)	\
	do { isb(); __raw_writel(OSLOCK_MAGIC, base + DBGLAR); } while (0)
#define DBG_LOCK(base)		\
	do { __raw_writel(0x1, base + DBGLAR); isb(); } while (0)
#define DBG_OS_UNLOCK(base)	\
	do { isb(); __raw_writel(0, base + DBGOSLAR); } while (0)
#define DBG_OS_LOCK(base)	\
	do { __raw_writel(0x1, base + DBGOSLAR); isb(); } while (0)

struct exynos_ehld_main {
	raw_spinlock_t			lock;
	unsigned int			cs_base;
	int				enabled;
	bool				suspending;
	bool				resuming;
	bool				need_to_task;
};

struct exynos_ehld_main ehld_main = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(ehld_main.lock),
};

struct exynos_ehld_data {
	unsigned long long		time[NUM_TRACE];
	unsigned long long		event[NUM_TRACE];
	unsigned long long		pmpcsr[NUM_TRACE];
	unsigned long			data_ptr;
};

struct exynos_ehld_ctrl {
	struct task_struct		*task;
	struct perf_event		*event;
	struct exynos_ehld_data		data;
	void __iomem			*dbg_base;
	int				ehld_running;
	raw_spinlock_t			lock;
};

static DEFINE_PER_CPU(struct exynos_ehld_ctrl, ehld_ctrl) =
{
	.lock = __RAW_SPIN_LOCK_UNLOCKED(ehld_ctrl.lock),
};

static struct perf_event_attr exynos_ehld_attr = {
	.type           = PERF_TYPE_HARDWARE,
	.config         = PERF_COUNT_HW_INSTRUCTIONS,
	.size           = sizeof(struct perf_event_attr),
	.sample_period  = GENMASK_ULL(31, 0),
	.pinned         = 1,
	.disabled       = 1,
};

static void exynos_ehld_callback(struct perf_event *event,
			       struct perf_sample_data *data,
			       struct pt_regs *regs)
{
	event->hw.interrupts++;       /* throttle interrupts */
}

static int exynos_ehld_start_cpu(unsigned int cpu)
{
	struct exynos_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event = ctrl->event;

	if (!event) {
		event = perf_event_create_kernel_counter(&exynos_ehld_attr, cpu, NULL,
							 exynos_ehld_callback, NULL);
		if (IS_ERR(event)) {
			ehld_printk(1, "@%s: cpu%d event make failed err: %ld\n",
							__func__, cpu, PTR_ERR(event));
			return PTR_ERR(event);
		} else {
			ehld_printk(1, "@%s: cpu%d event make success\n", __func__, cpu);
		}
		ctrl->event = event;
	}

	if (event) {
		ehld_printk(1, "@%s: cpu%d event enabled\n", __func__, cpu);
		perf_event_enable(event);
		ctrl->ehld_running = 1;
	}

	return 0;
}

static int exynos_ehld_stop_cpu(unsigned int cpu)
{
	struct exynos_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event = ctrl->event;

	if (event) {
		ctrl->ehld_running = 0;
		perf_event_disable(event);
		perf_event_release_kernel(event);
		ctrl->event = NULL;

		ehld_printk(1, "@%s: cpu%d event disabled\n", __func__, cpu);
	}

	return 0;
}

unsigned long long exynos_ehld_event_read_cpu(int cpu)
{
	struct exynos_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event = ctrl->event;
	unsigned long long total = 0;
	unsigned long long enabled, running;

	if (!in_irq() && event) {
		total = perf_event_read_value(event, &enabled, &running);
		ehld_printk(0, "%s: cpu%d - enabled: %llu, running: %llu, total: %llu\n",
				__func__, cpu, enabled, running, total);
	}
	return total;
}

void exynos_ehld_event_raw_update_allcpu(void)
{
	struct exynos_ehld_ctrl *ctrl;
	struct exynos_ehld_data *data;
	unsigned long long val;
	unsigned long flags, count;
	unsigned int cpu;

	if (dbg_snapshot_get_sjtag_status() == true)
		return;

	raw_spin_lock_irqsave(&ehld_main.lock, flags);
	for_each_possible_cpu(cpu) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);

		if (ctrl->event) {
			raw_spin_lock_irqsave(&ctrl->lock, flags);
			data = &ctrl->data;
			count = ++data->data_ptr & (NUM_TRACE - 1);
			data->time[count] = cpu_clock(cpu);
			if (cpu_is_offline(cpu) || !exynos_cpu.power_state(cpu) ||
				!ctrl->ehld_running) {
				ehld_printk(0, "%s: cpu%d is turned off : running:%x, power:%x, offline:%ld\n",
					__func__, cpu, ctrl->ehld_running, exynos_cpu.power_state(cpu), cpu_is_offline(cpu));
				data->event[count] = 0xC2;
				data->pmpcsr[count] = 0;
			} else {
				ehld_printk(0, "%s: cpu%d is turned on : running:%x, power:%x, offline:%ld\n",
					__func__, cpu, ctrl->ehld_running, exynos_cpu.power_state(cpu), cpu_is_offline(cpu));
				DBG_UNLOCK(ctrl->dbg_base + PMU_OFFSET);
				val = __raw_readq(ctrl->dbg_base + PMU_OFFSET + PMUPCSR);
				if (MSB_MASKING == (MSB_MASKING & val))
					val |= MSB_PADDING;
				data->pmpcsr[count] = val;
				data->event[count] = __raw_readl(ctrl->dbg_base + PMU_OFFSET);

				ehld_printk(0, "%s: cpu%d: 0x400:%x, 0xC00:%x, 0xE04:%x\n",
						__func__, cpu,
						__raw_readl(ctrl->dbg_base + PMU_OFFSET + 0x400),
						__raw_readl(ctrl->dbg_base + PMU_OFFSET + 0xC00),
						__raw_readl(ctrl->dbg_base + PMU_OFFSET + 0xE04));

				DBG_LOCK(ctrl->dbg_base + PMU_OFFSET);
			}
			raw_spin_unlock_irqrestore(&ctrl->lock, flags);
			ehld_printk(0, "%s: cpu%x - time:%llu, event:0x%llx\n",
				__func__, cpu, data->time[count], data->event[count]);
		}
	}
	raw_spin_unlock_irqrestore(&ehld_main.lock, flags);
}

void exynos_ehld_event_raw_dump_allcpu(void)
{
	struct exynos_ehld_ctrl *ctrl;
	struct exynos_ehld_data *data;
	unsigned long flags, count;
	int cpu, i;
	char symname[KSYM_NAME_LEN];

	if (dbg_snapshot_get_sjtag_status() == true)
		return;

	symname[KSYM_NAME_LEN - 1] = '\0';
	raw_spin_lock_irqsave(&ehld_main.lock, flags);
	ehld_printk(1, "--------------------------------------------------------------------------\n"
		"      Exynos Early Lockup Detector Information\n\n"
		"      CPU    NUM     TIME                 Value                PC\n\n");
	for_each_possible_cpu(cpu) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		data = &ctrl->data;
		i = 0;
		do {
			count = ++data->data_ptr & (NUM_TRACE - 1);
			symname[KSYM_NAME_LEN - 1] = '\0';
			i++;

			if (sprint_symbol(symname, data->pmpcsr[count]) < 0)
				symname[0] = '\0';

			ehld_printk(1, "      %03d    %03d     %015llu      0x%015llx      0x%016llx(%s)\n",
					cpu, i,
					(unsigned long long)data->time[count],
					(unsigned long long)data->event[count],
					(unsigned long long)data->pmpcsr[count],
					symname);

			if (i >= NUM_TRACE)
				break;
		} while (1);
		ehld_printk(1, "      -----------------------------------------------------\n");
	}
	raw_spin_unlock_irqrestore(&ehld_main.lock, flags);
}

static int exynos_ehld_cpu_online(unsigned int cpu)
{
	struct exynos_ehld_ctrl *ctrl;
	unsigned long flags;

	ctrl = per_cpu_ptr(&ehld_ctrl, cpu);

	raw_spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->ehld_running = 1;
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);

	return 0;
}

static int exynos_ehld_cpu_predown(unsigned int cpu)
{
	struct exynos_ehld_ctrl *ctrl;
	unsigned long flags;

	ctrl = per_cpu_ptr(&ehld_ctrl, cpu);

	raw_spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->ehld_running = 0;
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);

	return 0;
}

int exynos_ehld_start(void)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		exynos_ehld_start_cpu(cpu);
	put_online_cpus();

	return 0;
}

void exynos_ehld_stop(void)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		exynos_ehld_stop_cpu(cpu);
	put_online_cpus();
}

#ifdef EHLD_TASK_SUPPORT
static int ehld_task_should_run(unsigned int cpu)
{
	if (ehld_main.suspending && ehld_main.need_to_task)
		return 1;
	else
		return 0;
}

static void ehld_task_run(unsigned int cpu)
{
	if (ehld_main.resuming)
		exynos_ehld_start_cpu(0);
	else
		exynos_ehld_stop_cpu(0);

	/* Done to progress of calling task */
	ehld_main.need_to_task = false;
}

static void ehld_disable(unsigned int cpu)
{
	struct exynos_ehld_ctrl *ctrl;

	/*
	 * It tries to shutdown CPU0 of perf events
	 * smp_hotplug_thread don't support on CPU0
	 */
	if (cpu == 1) {
		ehld_main.need_to_task = true;

		/* This task shouldn't be processed in resuming */
		ehld_main.resuming = false;
		ctrl = per_cpu_ptr(&ehld_ctrl, 0);
		wake_up_process(ctrl->task);
	}

	exynos_ehld_stop_cpu(cpu);
}

static void ehld_enable(unsigned int cpu)
{
	struct exynos_ehld_ctrl *ctrl;

	/*
	 * It tries to shutdown CPU0 of perf events
	 * smp_hotplug_thread don't support on CPU0
	 */
	if (cpu == 1) {
		ehld_main.need_to_task = true;

		/* This task should be processed in resuming */
		ehld_main.resuming = true;
		ctrl = per_cpu_ptr(&ehld_ctrl, 0);
		wake_up_process(ctrl->task);
	}

	exynos_ehld_start_cpu(cpu);
}

static void ehld_cleanup(unsigned int cpu, bool online)
{
	ehld_disable(cpu);
}

static struct smp_hotplug_thread ehld_threads = {
	.store			= &ehld_ctrl.task,
	.thread_should_run	= ehld_task_should_run,
	.thread_fn		= ehld_task_run,
	.thread_comm		= "ehld_task/%u",
	.setup			= ehld_enable,
	.cleanup		= ehld_cleanup,
	.park			= ehld_disable,
	.unpark			= ehld_enable,
};
#else
static enum cpuhp_state hp_online;
void exynos_ehld_shutdown(void)
{
	struct perf_event *event;
	struct exynos_ehld_ctrl *ctrl;
	int cpu;

	cpuhp_remove_state(hp_online);

	for_each_possible_cpu(cpu) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		event = ctrl->event;
		if (!event)
			continue;
		perf_event_disable(event);
		ctrl->event = NULL;
		perf_event_release_kernel(event);
	}
}
#endif

#define NUM_TRACE_HARDLOCKUP	(NUM_TRACE / 3)
#ifdef CONFIG_HARDLOCKUP_DETECTOR_OTHER_CPU
extern struct atomic_notifier_head hardlockup_notifier_list;

static int exynos_ehld_hardlockup_handler(struct notifier_block *nb,
					   unsigned long l, void *p)
{
	int i;

	for (i = 0; i < NUM_TRACE_HARDLOCKUP; i++)
		exynos_ehld_event_raw_update_allcpu();

	exynos_ehld_event_raw_dump_allcpu();
	return 0;
}

static struct notifier_block exynos_ehld_hardlockup_block = {
	.notifier_call = exynos_ehld_hardlockup_handler,
};

static void register_hardlockup_notifier_list(void)
{
	atomic_notifier_chain_register(&hardlockup_notifier_list,
					&exynos_ehld_hardlockup_block);
}
#else
static void register_hardlockup_notifier_list(void) {}
#endif

static int exynos_ehld_panic_handler(struct notifier_block *nb,
					   unsigned long l, void *p)
{
	int i;

	for (i = 0; i < NUM_TRACE_HARDLOCKUP; i++)
		exynos_ehld_event_raw_update_allcpu();

	exynos_ehld_event_raw_dump_allcpu();
	return 0;
}

static struct notifier_block exynos_ehld_panic_block = {
	.notifier_call = exynos_ehld_panic_handler,
};

static int exynos_ehld_c2_pm_notifier(struct notifier_block *self,
						unsigned long action, void *v)
{
	int cpu = raw_smp_processor_id();

	switch (action) {
	case CPU_PM_ENTER:
		exynos_ehld_cpu_predown(cpu);
		break;
        case CPU_PM_ENTER_FAILED:
        case CPU_PM_EXIT:
		exynos_ehld_cpu_online(cpu);
		break;
	case CPU_CLUSTER_PM_ENTER:
		exynos_ehld_cpu_predown(cpu);
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		exynos_ehld_cpu_online(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block exynos_ehld_c2_pm_nb = {
	.notifier_call = exynos_ehld_c2_pm_notifier,
};

static int exynos_ehld_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	/*
	 * We should control re-init / exit for all CPUs
	 * Originally all CPUs are controlled by cpuhp framework.
	 * But CPU0 is not controlled by cpuhp framework in exynos BSP.
	 * So mainline code of perf(kernel/cpu.c) for CPU0 is not called by cpuhp framework.
	 * As a result, it's OK to not control CPU0.
	 * CPU0 will be controlled by CPU_PM notifier call.
	 */

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		ehld_main.suspending = 1;
		break;

	case PM_POST_SUSPEND:
		ehld_main.suspending = 0;
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos_ehld_nb = {
	.notifier_call = exynos_ehld_pm_notifier,
};

static int exynos_ehld_init_dt_parse(struct device_node *np)
{
	struct device_node *child;
	int ret = 0, cpu = 0;
	unsigned int offset, base;
	char name[SZ_16];
	struct exynos_ehld_ctrl *ctrl;

	if (of_property_read_u32(np, "cs_base", &base)) {
		ehld_printk(1, "exynos-ehld: no coresight base address in device tree\n");
		return -EINVAL;
	}
	ehld_main.cs_base = base;

	for_each_possible_cpu(cpu) {
		snprintf(name, sizeof(name), "cpu%d", cpu);
		child = of_get_child_by_name(np, (const char *)name);

		if (!child) {
			ehld_printk(1, "exynos-ehld: device tree is not completed - cpu%d\n", cpu);
			return -EINVAL;
		}

		ret = of_property_read_u32(child, "dbg-offset", &offset);
		if (ret)
			return -EINVAL;

		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		ctrl->dbg_base = ioremap(ehld_main.cs_base + offset, SZ_256K);

		if (!ctrl->dbg_base) {
			ehld_printk(1, "exynos-ehld: fail ioremap for dbg_base of cpu%d\n", cpu);
			return -ENOMEM;
		}

		ehld_printk(1, "exynos-ehld: cpu#%d, cs_base:0x%x, dbg_base:0x%x, total:0x%x, ioremap:0x%lx\n",
				cpu, base, offset, ehld_main.cs_base + offset,
				(unsigned long)ctrl->dbg_base);
	}
	return ret;
}

static const struct of_device_id ehld_of_match[] = {
	{ .compatible	= "exynos-ehld" },
	{},
};

static int exynos_ehld_setup(void)
{
	struct exynos_ehld_ctrl *ctrl;
	int cpu;

	/* register pm notifier */
	register_pm_notifier(&exynos_ehld_nb);

	/* register cpu pm notifier for C2 */
	cpu_pm_register_notifier(&exynos_ehld_c2_pm_nb);

	register_hardlockup_notifier_list();

	atomic_notifier_chain_register(&panic_notifier_list, &exynos_ehld_panic_block);

	for_each_possible_cpu(cpu) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		memset((void *)&ctrl->data, 0, sizeof(struct exynos_ehld_data));
	}

#ifdef EHLD_TASK_SUPPORT
	return smpboot_register_percpu_thread(&ehld_threads);
#else
	return cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "exynos-ehld:online",
				exynos_ehld_start_cpu, exynos_ehld_stop_cpu);
#endif
}

static int ehld_probe(struct platform_device *pdev)
{
	int err = 0;

	err = exynos_ehld_init_dt_parse(pdev->dev.of_node);
	if (err) {
		ehld_printk(1, "exynos-ehld: fail to process device tree for ehld:%d\n", err);
		return err;
	}

	err = exynos_ehld_setup();
	if (err) {
		ehld_printk(1, "exynos-ehld: fail to process setup for ehld:%d\n", err);
		return err;
	}

	ehld_printk(1, "exynos-ehld: success to initialize\n");

	return 0;
}

static int ehld_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver exynos_ehld_driver = {
	.probe = ehld_probe,
	.remove = ehld_remove,
	.driver = {
		.name = "ehld",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ehld_of_match),
	},
};
module_platform_driver(exynos_ehld_driver);

MODULE_DESCRIPTION("Samsung Exynos EHLD COMMON DRIVER");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exynos-ehld");
