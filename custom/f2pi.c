/*
 * f2pi.c -- Raspberry Pi Model F keyboard device driver using the mini UART.
 *
 * Copyright 2017 mincepi
 *
 * https://sites.google.com/site/mincepi/f2pi
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the Linux kernel source for more details.
 *
 * The following resources helped me greatly:
 *
 * The matrix-keypad Linux kernel module: matrix_keypad.c
 *
 * The platform device API
 * Copyright (C) 2011 Eklektix, Inc.
 *
 * The Linux kernel module programming howto
 * Copyright (C) 2001 Jay Salzman
 *
 * The Linux USB input subsystem Part 1
 * Copyright (C) 2007 Brad Hards
 *
 * PS/2 keyboard interfacing
 * Copyright (C) 1998-2013 Adam Chapweske
 *
 * kbd FAQ
 * Copyright (C) 2009 Andries Brouwer
 *
 * Keyboard interface circuit is on my website.
 *
 * Add core_freq=250 to /boot/config.txt
 *
 * Add dtoverlay=f2pi to the end of /boot/config.txt and copy f2pi.dtbo to /boot/overlays.
 *
 * Use companion userspace program ftest to determine the exact clock divider.
 * Enter these values into f2pi.dts, compile it and copy to /boot/overlays.
 *
 * You don't need to do anything special to compile this module.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/types.h>

//uart register offsets
#define IRQ		0x00
#define ENB		0x01
#define IIR		0x12
#define IER		0x11
#define LCR		0x13
#define MCR		0x14
#define CNTL		0x18
#define BAUD		0x1a
#define IO		0x10
#define LSR		0x15
#define STAT		0x19

volatile unsigned *(uart);
static struct input_dev *f2pi;
struct platform_device *pdev;
static uint8_t translate[256] = {0};
int irq;

//device tree stuff
static const struct of_device_id f2pi_dt_ids[] = {
	{ .compatible = "f2pi" },
	{},
};

MODULE_DEVICE_TABLE(of, f2pi_dt_ids);

// handle uart interrupt: read uart and send to event subsystem
static irqreturn_t irq_handler(int irq, void *dev_id)
{
    static unsigned key;

    if  ((ioread32(uart + IRQ) & 1) == 0)
    {
	return IRQ_NONE;
    }

    //process key while data available
    while ((ioread32(uart + LSR) & 1) != 0)
    {	
	key = ioread32(uart + IO);
	input_report_key(f2pi, (~key) & 0x7f, key >> 7);
	input_sync(f2pi);
    }

    return IRQ_HANDLED;

}

//set up
static int f2pi_probe(struct platform_device *pdev)
{
    static uint32_t divider;
    static int i, retval;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    struct resource *res;

    //set up input event device
    f2pi=input_allocate_device();
    f2pi->name = "f2pi";
    f2pi->phys = "f2pi/input0";
    f2pi->id.bustype = BUS_HOST;
    f2pi->id.vendor = 0x0001;
    f2pi->id.product = 0x0001;
    f2pi->id.version = 0x0100;
    f2pi->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
    f2pi->keycode = translate;
    f2pi->keycodesize = sizeof(unsigned char);
    f2pi->keycodemax = 127;
    for (i = 0; i < 127; i++) set_bit(i,f2pi->keybit);
    retval = input_register_device(f2pi);
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    uart = ioremap(res->start, 0x80);

    //set baud rate from device tree
    of_property_read_u32(np, "divider", &divider);

    //set gpio 14 high to power interface
    gpio_set_value(14, 1);

    //enable uart
    iowrite32(ioread32(uart + ENB) | 1, uart + ENB);

    //disable interrupts
    iowrite32(0, uart + IER);

    //disable rx,tx
    iowrite32(0, uart + CNTL);

    //set uart to receive 8 bit
    iowrite32(3, uart + LCR);

    //disable modem control
    iowrite32(0, uart + MCR);

    //enable rx interrupts
    iowrite32(5, uart + IER);

    //clear fifo
    iowrite32(0xc6, uart + IIR);

    //set baud rate
    iowrite32(divider, uart + BAUD);

    //enable rx
    iowrite32(3, uart + CNTL);

    irq = platform_get_irq(pdev, 0);

    retval = request_irq(irq, (irq_handler_t)irq_handler, 0, "f2pi", (void *)irq_handler);

    printk(KERN_INFO "f2pi: divider %i\n", divider);
    return 0;
}

//tear down
static int f2pi_remove(struct platform_device *pdev)
{
    //disable interrupts
    iowrite32(0, uart + IER);

    //disable rx,tx
    iowrite32(0, uart + CNTL);

    //disable uart
    iowrite32(ioread32(uart + ENB) & 0xfe, uart + ENB);

    free_irq(irq, (void *)irq_handler);
    iounmap(uart);
    input_unregister_device(f2pi);

    return 0;
}

static struct platform_driver f2pi_driver = {
	.probe	=	f2pi_probe,
	.remove	=	f2pi_remove,
	.driver = {
		.name	= "f2pi",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(f2pi_dt_ids),
	},
};

//register driver
static int __init f2pi_init(void)
{
    printk(KERN_INFO "f2pi: loaded\n");
    return platform_driver_register(&f2pi_driver);
    return 0;
}

//unregister driver
static void __exit f2pi_exit(void)
{
    platform_driver_unregister(&f2pi_driver);
    printk(KERN_INFO "f2pi: unloaded\n");
    return;
}

module_init(f2pi_init);
module_exit(f2pi_exit);

MODULE_LICENSE("GPL");
