/*
 * INGENIC SOC Setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Ingenic Semiconductor Inc.
 */
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <linux/libfdt.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <asm/prom.h>
#include <soc/cpm.h>
#include <soc/base.h>


extern void *get_fdt_addr(void);
extern void __init soc_is_x1000e(void);

void __init plat_mem_setup(void)
{
	void __iomem *cpm_iobase = (void __iomem *)CKSEG1ADDR(CPM_IOBASE);

	/* ingenic mips cpu special */
	__asm__ (
		"li    $2, 0xa9000000 \n\t"
		"mtc0  $2, $5, 4      \n\t"
		"nop                  \n\t"
		::"r"(2));

	set_io_port_base(IO_BASE);
	/*Not have ioport*/
	ioport_resource.start	= 0x00000000;
	ioport_resource.end	= 0x00000000;
	iomem_resource.start	= 0x10000000;
	iomem_resource.end	= 0x1fffffff;

	/*x1000 cpu special*/
	writel( 0, cpm_iobase + CPM_PSWC0ST);
	writel(16, cpm_iobase + CPM_PSWC1ST);
	writel(24, cpm_iobase + CPM_PSWC2ST);
	writel( 8, cpm_iobase + CPM_PSWC3ST);

	__dt_setup_arch(get_fdt_addr());

	early_init_fdt_scan_reserved_mem();

	soc_is_x1000e();

	return;
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}

static int __init plat_of_populate(void)
{
	of_platform_default_populate(NULL, NULL, NULL);
	return 0;
}
arch_initcall(plat_of_populate);

void __init plat_time_init(void)
{
	of_clk_init(NULL);

	timer_probe();
}

void __init arch_init_irq(void) {

	irqchip_init();
}
