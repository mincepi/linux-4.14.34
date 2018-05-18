/*
 *   mtest.c -- Raspberry Pi Model M keyboard test program.
 *   Used to determine m2pi module load parameters.
 *
 *   Copyright 2017 mincepi 
 *
 *   https://sites.google.com/site/mincepi/f2pi
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Some of the code is copied from the GPIO register access example program 
 *   by Dom and Gert.  It had no license notice.
 */

//To compile this program: gcc mtest.c -o mtest
//Make sure /boot/config.txt includes core_freq=250
//Make sure the m2pi module is not loaded, then run mtest as root.
//The keyboard should already be connected.
//Follow the onscreen instructions.
//Tested with gcc 6.3.0

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

//gpio
#define GPFSEL1		0x200004/4

//uart
#define ENB		0x215004/4
#define IIR		0x215048/4
#define IER		0x215044/4
#define LCR		0x21504c/4
#define CNTL		0x215060/4
#define BAUD		0x215068/4
#define IO		0x215040/4
#define LSR		0x215054/4
#define STAT		0x215064/4


int  fd;
void *io_base_map;
volatile unsigned *io_base;
unsigned buf[2];

void setup_io()
{
    fd = open("/sys/firmware/devicetree/base/soc/ranges", O_RDONLY|O_SYNC);
    read(fd, &buf, 8);
    close(fd);

   // open /dev/mem
   if ((fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem n");
      exit(-1);
   }

   //mmap IO_BASE
   io_base_map = mmap(
      NULL,             				//Any adddress in our space will do
      0x400000,       					//Map length
      PROT_READ|PROT_WRITE,				//Enable reading & writting to mapped memory
      MAP_SHARED,       				//Shared with other processes
      fd,           					//File to map
      buf[1] << 24         				//Offset to IO peripherals
   );

   close(fd); 						//No need to keep fd open after mmap

   if (io_base_map == MAP_FAILED) {
      printf("mmap error'a0%dn", (int)io_base_map);	//errno also set!
      exit(-1);
   }

    // Always use volatile pointer!
    io_base = (volatile unsigned *)io_base_map;

    //set pin 15 as uart1 rx
    *(io_base+GPFSEL1) &= ~(7<<15);
    *(io_base+GPFSEL1) |= (2<<15);

    //enable uart
    *(io_base+ENB) = 1;

    //disable interrupts
    *(io_base+IER) = 0;

    //set uart to receive 8 bit
    *(io_base+LCR) = 3;

    //clear fifo
    *(io_base+IIR) = 2;

    //enable rx
    *(io_base+CNTL) = 1;
}

void main(int argc, char **argv) 
{

    unsigned baud, low, high, key, i;

    setup_io();

    //coarse divisor calculate
    baud = 2000;
    printf("Press and hold the comma key on the Model M keyboard.\n");
    fflush(NULL);

    do
    {

	*(io_base + BAUD) = baud;
	baud += 10;
	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character

    } while ((*(io_base + IO) & 0xff) != 65);

    low = baud;
    baud += 100;
    printf("phase 1 complete\n");
    fflush(NULL);

    //discard several characters
    for (i=0; i<4; i++)
    {

	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character
	key = *(io_base + IO);

    }

    do
    {

	*(io_base + BAUD) = baud;
	baud += 10;
	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character

    } while ((*(io_base + IO) & 0xff) == 65);

    high = baud;
    baud = low + (((high - low) / 4));
    printf("phase 2 complete\n");
    fflush(NULL);

    //discard several characters
    for (i=0; i<4; i++)
    {

	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character
	key = *(io_base + IO);

    }

    //fine divisor calculate
    do
    {

	*(io_base + BAUD) = baud;
	baud -= 2;
	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character

    } while ((*(io_base + IO) & 0xff) == 65);

    low = baud;
    baud = low + (((high - low) * 3) / 4);
    printf("phase 3 complete\n");
    fflush(NULL);

    //discard several characters
    for (i=0; i<4; i++)
    {

	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character
	key = *(io_base + IO);

    }

    do
    {

	*(io_base + BAUD) = baud;
	baud += 2;
	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character

    } while ((*(io_base + IO) & 0xff) == 65);

    high = baud;
    printf("suggested divider %u\n", low + ((high - low) / 2));
    fflush(NULL);


baud = low + ((high - low) / 2);
*(io_base + BAUD) = baud;

    for (;;)
    {

	while ((*(io_base + LSR) & 1) == 0) { }   //wait for character
	printf("key %u\n", *(io_base + IO) & 0xff);
	fflush(NULL);

    }








    return;
}

