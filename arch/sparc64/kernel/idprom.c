/* $Id: idprom.c,v 1.1 1996/12/28 18:39:38 davem Exp $
 * idprom.c: Routines to load the idprom into kernel addresses and
 *           interpret the data contained within.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

#include <asm/oplib.h>
#include <asm/idprom.h>

struct idprom *idprom;
static struct idprom idprom_buffer;

/* Calculate the IDPROM checksum (xor of the data bytes). */
__initfunc(static unsigned char calc_idprom_cksum(struct idprom *idprom))
{
	unsigned char cksum, i, *ptr = (unsigned char *)idprom;

	for (i = cksum = 0; i <= 0x0E; i++)
		cksum ^= *ptr++;

	return cksum;
}

/* Create a local IDPROM copy and verify integrity. */
__initfunc(void idprom_init(void))
{
	prom_get_idprom((char *) &idprom_buffer, sizeof(idprom_buffer));

	idprom = &idprom_buffer;

	if (idprom->id_format != 0x01)  {
		prom_printf("IDPROM: Unknown format type!\n");
		prom_halt();
	}

	if (idprom->id_cksum != calc_idprom_cksum(idprom)) {
		prom_printf("IDPROM: Checksum failure (nvram=%x, calc=%x)!\n",
			    idprom->id_cksum, calc_idprom_cksum(idprom));
		prom_halt();
	}

	printk("Ethernet address: %02x:%02x:%02x:%02x:%02x:%02x\n",
	       idprom->id_ethaddr[0], idprom->id_ethaddr[1],
	       idprom->id_ethaddr[2], idprom->id_ethaddr[3],
	       idprom->id_ethaddr[4], idprom->id_ethaddr[5]);
}