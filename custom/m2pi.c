/*
 * m2pi.c -- Raspberry Pi Model M keyboard device driver using the mini UART.
 *
 * Copyright 2017 mincepi
 *
 * https://sites.google.com/site/mincepi/m2pi
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
 * Use companion userspace program mtest to determine the exact clock divider.
 * Enter these values into m2pi.dts, compile it and copy to /boot/overlays.
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

static unsigned keyup = 0;
static unsigned escape = 0;
static unsigned pause = 0;
volatile unsigned *(uart);
static struct input_dev *m2pi;
struct platform_device *pdev;
int irq;

/* Raw SET 2 scancode to linux keycode translation table, filled from device tree overlay during probe */
static uint8_t translate[256] = {0};

//device tree stuff
static const struct of_device_id m2pi_dt_ids[] = {
	{ .compatible = "m2pi" },
	{},
};

MODULE_DEVICE_TABLE(of, m2pi_dt_ids);

// handle uart interrupt: read uart and send to event subsystem
static irqreturn_t irq_handler(int irq, void *dev_id)
{
    static unsigned key;

    if  ((ioread32(uart + IRQ) & 1) == 0)
    {
	return IRQ_NONE;
    }

    //process key while data is available
    while ((ioread32(uart + LSR) & 1) != 0)
    {	
	key = ioread32(uart + IO) & 0xff;

	if (key == 0xff) {
    	    return IRQ_HANDLED;
	}

	if (key == 0xf0) {
    	    keyup = 1;
    	    return IRQ_HANDLED;
	}

	if (key == 0xe0) {
    	    escape = 1;
    	    return IRQ_HANDLED;
	}

	if (key == 0xe1) {
    	    pause = 2;
    	    return IRQ_HANDLED;
	}

	if (pause == 2) {
    	    pause = 1;
    	    return IRQ_HANDLED;
	}

	if (pause == 1) {
    	    key = 0x88;
    	    pause = 0;
	}

	if (escape == 1) {
    	    key |= 0x80;
    	    escape = 0;
	}

	key = translate[key];

	if (keyup == 1) {
    	    input_report_key(m2pi,key,0);
    	    keyup = 0;
	} else {
	    input_report_key(m2pi,key,1);
	}

	input_sync(m2pi);

    }

    return IRQ_HANDLED;

}

//set up
static int m2pi_probe(struct platform_device *pdev)
{
    static uint32_t divider;
    static int i, retval;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    struct resource *res;

    //set up input event device
    of_property_read_u8_array(np, "translate", &translate[0], 256);
    m2pi=input_allocate_device();
    m2pi->name = "m2pi";
    m2pi->phys = "m2pi/input0";
    m2pi->id.bustype = BUS_HOST;
    m2pi->id.vendor = 0x0001;
    m2pi->id.product = 0x0001;
    m2pi->id.version = 0x0100;
    m2pi->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
    m2pi->keycode = translate;
    m2pi->keycodesize = sizeof(unsigned char);
    m2pi->keycodemax = 255;
    for (i = 0; i < 256; i++) set_bit(i, m2pi->keybit);
    retval = input_register_device(m2pi);
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    uart = ioremap(res->start, 0x80);

    //set baud rate from device tree
    of_property_read_u32(np, "divider", &divider);

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

    //set up interrupt handler
    irq = platform_get_irq(pdev, 0);
    retval = request_irq(irq, (irq_handler_t)irq_handler, 0, "m2pi", (void *)irq_handler);
    printk(KERN_INFO "m2pi: divider %i\n", divider);
    return 0;
}

//tear down
static int m2pi_remove(struct platform_device *pdev)
{
    //disable interrupts
    iowrite32(0, uart + IER);

    //disable rx
    iowrite32(0, uart + CNTL);

    //disable uart
    iowrite32(ioread32(uart + ENB) & 0xfe, uart + ENB);

    free_irq(irq, (void *)irq_handler);
    iounmap(uart);
    input_unregister_device(m2pi);

    return 0;
}

static struct platform_driver m2pi_driver = {
	.probe	=	m2pi_probe,
	.remove	=	m2pi_remove,
	.driver = {
		.name	= "m2pi",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(m2pi_dt_ids),
	},
};

//register driver
static int __init m2pi_init(void)
{
    printk(KERN_INFO "m2pi: loaded\n");
    return platform_driver_register(&m2pi_driver);
    return 0;
}

//unregister driver
static void __exit m2pi_exit(void)
{
    platform_driver_unregister(&m2pi_driver);
    printk(KERN_INFO "m2pi: unloaded\n");
    return;
}

module_init(m2pi_init);
module_exit(m2pi_exit);

MODULE_LICENSE("GPL");
