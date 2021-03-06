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


static void __init cpm_reset(void)
{
#ifndef CONFIG_FPGA_TEST
        unsigned long clkgr = cpm_inl(CPM_CLKGR);

        clkgr &= ~(1 << 28      /* DES */
                | 1 << 26       /* TVE */
                | 1 << 13       /* SADC */
                );
        cpm_outl(clkgr, CPM_CLKGR);

        /* TODO set default clkgr here */
#endif
}

static int __init setup_init(void)
{
        cpm_reset();
        /* Set bus priority here */
        *(volatile unsigned int *)0xb34f0240 = 0x00010003;
        *(volatile unsigned int *)0xb34f0244 = 0x00010003;

        return 0;
}


void __init plat_mem_setup(void)
{

	set_io_port_base(IO_BASE);

	/*Not have ioport*/
	ioport_resource.start   = 0x00000000;
	ioport_resource.end     = 0xffffffff;
	iomem_resource.start    = 0x00000000;
	iomem_resource.end      = 0xffffffff;

	setup_init();

	__dt_setup_arch(get_fdt_addr());

	early_init_fdt_scan_reserved_mem();

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
