/*
 * pi2hd44780.c -- Raspberry Pi HD44780 driver using GPIO
 *
 * Copyright 2016 mincepi
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2 as used in the Linux kernel. See the file COPYING in the Linux kernel 
 *
 * The interface circuit and other critical instructions are available at:
 *
 * https://sites.google.com/site/mincepi/pi2hd44780
 *
 * No system call error checking is done in this module.
 * If device creation fails because the node already exists, well, that's just poor 
 * housekeeping on your part.  Besides, all that extra code would clutter things up and make it hard
 * for a novice to understand the program.  I'm also basically lazy, and anyway this is a quick hack.
 * 
 * Write to /dev/pi2hd44780 to put something on the display.
 * No character filtering is done so special characters can be displayed.
 * The display clears and resets to memory location 0 each time you open the device,
 * so write a whole string, not single characters.
 * If you need to write to the display from a non-root user
 * add the following line to the /etc/udev/rules.d/99-com.rules file:
 * KERNEL=="pi2hd44780", MODE="0660"
 * 
 * This module is for a 1x16 display salvaged from a LaserJet III.
 * It has a funny memory layout - the first 8 characters are in memory locations 0-7,
 * the last 8 characters are in memory locations 40-47.
 * This was probably done to increase contrast.
 * Unless you have one of these displays, you will need to modify the code.
 * There are comments in the appropriate locations.
 * It can be simply modified to handle most 1 or 2 line displays.
 * cr and lf are not handled. If you have a 2 line display
 * you'll have to pad the first line with spaces to write on the second line.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/io.h>

/*Change following defines if you wire differently
 *R/W, D0, D1, D2, D3 should all be grounded
 *PWM0 controls contrast, use the gpio utility to set.
 */
#define RS	14
#define E	15
#define D4	23
#define D5	24
#define D6	25
#define D7	8

static int position;
static dev_t Device;
static struct class* Class;
static struct cdev c_dev;

/* send nibble to display */
static void nibble(u8 nibble)
{
    if (nibble & 0x10) gpio_set_value(D4,1); else gpio_set_value(D4,0);
    if (nibble & 0x20) gpio_set_value(D5,1); else gpio_set_value(D5,0);
    if (nibble & 0x40) gpio_set_value(D6,1); else gpio_set_value(D6,0);
    if (nibble & 0x80) gpio_set_value(D7,1); else gpio_set_value(D7,0);
    gpio_set_value(E,1);
    udelay(1);
    gpio_set_value(E,0);
    udelay(1);
    return;
}

/* send byte to display in two nibbles with standard delay */
static void byte(u8 byte)
{
    nibble(byte);
    nibble(byte << 4);
    usleep_range(100, 200);
    return;
}

/* move address counter to position 40 */
static void jump(void)
{
    gpio_set_value(RS,0);
    byte(0xa8);
    gpio_set_value(RS,1);
    return;
}

/* clear display */
static int open(struct inode *inode, struct file *file)
{
    gpio_set_value(RS,0);
    byte(0x01);
    gpio_set_value(RS,1);
    position = 0;
    usleep_range(2000, 3000);
    return 0;
}

/* write data to display */
static ssize_t write(struct file *filp, const char *buffer, size_t length, loff_t * offset)
{
    static u8 value;

    /* truncate data if too long, adjust for your display */
    if (position > 15) return 1;

    /* correct address for 2-line display (or wonky 1 line display), adjust for your display */
    if (position == 8) jump();

    /* display one character. Maybe I should implement loop to do more... */
    get_user(value, buffer);
    byte(value);
    position++;
    return 1;
}

/* file operations struct */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = write,
    .open = open,
};

/* helper functions for GPIO setup */
void gpio_init_pin(int GPIO)
{
    gpio_request(GPIO, "sysfs");
    gpio_direction_output(GPIO, 1);
    gpio_export(GPIO, false);
}

void gpio_init(void)
{
    gpio_init_pin(RS);
    gpio_init_pin(E);
    gpio_init_pin(D4);
    gpio_init_pin(D5);
    gpio_init_pin(D6);
    gpio_init_pin(D7);
}

/* helper functions for GPIO teardown */
void gpio_cleanup_pin(int GPIO)
{
    gpio_set_value(GPIO, 0);
    gpio_unexport(GPIO);
    gpio_free(GPIO);
}

void gpio_cleanup(void)
{
    gpio_cleanup_pin(RS);
    gpio_cleanup_pin(E);
    gpio_cleanup_pin(D4);
    gpio_cleanup_pin(D5);
    gpio_cleanup_pin(D6);
    gpio_cleanup_pin(D7);
}

/* initialize module and display */
int init_module(void)
{
    /* set up device node and register*/
    alloc_chrdev_region(&Device, 0, 1, "pi2hd44780");
    Class = class_create(THIS_MODULE, "pi2hd44780");
    device_create(Class, NULL, MKDEV(MAJOR(Device), MINOR(Device)), NULL, "pi2hd44780");
    cdev_init(&c_dev, &fops);
    cdev_add(&c_dev, Device, 1);
    gpio_init();

    /* initialize to 4-bit mode using magic sequence */
    gpio_set_value(RS,0);
    nibble(0x30);
    usleep_range(5000, 6000);
    nibble(0x30);
    usleep_range(200, 300);
    nibble(0x30);
    usleep_range(100, 200);
    nibble(0x20);
    usleep_range(100, 200);

    /* initialize to 2-line and clear display */
    byte(0x06);
    byte(0x0c);
    byte(0x28);    /*change to 0x20 for proper single line display*/
    byte(0x01);
    usleep_range(2000, 3000);
    gpio_set_value(RS,1);

    printk(KERN_INFO "pi2hd44780: loaded\n");
    return 0;
}

/* tear down module and display */
void cleanup_module(void)
{
    /* remove node and unregister device */
    device_destroy(Class, MKDEV(MAJOR(Device), MINOR(Device)));
    class_destroy(Class);
    unregister_chrdev_region(Device, 1);
    cdev_del(&c_dev);
    gpio_cleanup();
    printk(KERN_INFO "pi2hd44780: unloaded\n");
}

MODULE_LICENSE("GPL");
