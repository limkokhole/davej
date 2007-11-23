/*
 * Definitions for the Mitsumi CDROM interface
 * (H) Hackright 1996 by Marcin Dalecki <dalecki@namu03.gwdg.de>
 * VERSION: 2.5
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MCDX_H
#define __MCDX_H
/*
 * 	PLEASE CONFIGURE THIS ACCORDING TO YOUR HARDWARE/JUMPER SETTINGS.
 *
 *      o       MCDX_NDRIVES  :  number of used entries of the following table
 *      o       MCDX_DRIVEMAP :  table of {i/o base, irq} per controller
 *
 *      NOTE: Don't even think about connecting the drive to IRQ 9(2).
 *	In the AT architecture this interrupt is used to cascade the two
 *	interrupt controllers and isn't therefore usable for anything else!
 */
 /* #define I_WAS_IN_MCDX_H */
#define MCDX_NDRIVES 1
#define MCDX_DRIVEMAP {	{0x230, 11},	\
			{0x304, 05},  	\
			{0x000, 00},  	\
			{0x000, 00},  	\
			{0x000, 00},  	\
}
	  	
/* 
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!NO USER INTERVENTION NEEDED BELOW
 * If You are sure that all configuration is done, please uncomment the
 * line below. 
 */

#undef MCDX_DEBUG	/* This is *REALLY* only for development! */

#ifdef MCDX_DEBUG
#define MCDX_TRACE(x) printk x
#define MCDX_TRACE_IOCTL(x) printk x
#else
#define MCDX_TRACE(x)
#define MCDX_TRACE_IOCTL(x)
#endif

/*      The name of the device */
#define MCDX "mcdx"

/*
 *      Per controller 4 bytes i/o are needed. 
 */
#define MCDX_IO_SIZE		4

/* 
 * Masks for the status byte, returned from every command, set if
 * the description is true 
 */
#define MCDX_RBIT_OPEN       0x80	/* door is open */
#define MCDX_RBIT_DISKSET    0x40	/* disk set (recognised) */
#define MCDX_RBIT_CHANGED    0x20	/* disk was changed */
#define MCDX_RBIT_CHECK      0x10	/* disk rotates, servo is on */
#define MCDX_RBIT_AUDIOTR    0x08	/* current track is audio */
#define MCDX_RBIT_RDERR      0x04	/* read error, refer SENSE KEY */
#define MCDX_RBIT_AUDIOBS    0x02	/* currently playing audio */
#define MCDX_RBIT_CMDERR     0x01	/* command, param or format error */

/* 
 * The I/O Register holding the h/w status of the drive,
 * can be read at i/o base + 1 
 */
#define MCDX_RBIT_DOOR       0x10	/* door is open */
#define MCDX_RBIT_STEN       0x04	/* if 0, i/o base contains drive status */
#define MCDX_RBIT_DTEN       0x02	/* if 0, i/o base contains data */

/*
 *    The commands.
 */
#define MCDX_CMD_GET_TOC		0x10
#define MCDX_CMD_GET_MDISK_INFO		0x11
#define MCDX_CMD_GET_SUBQ_CODE		0x20
#define MCDX_CMD_GET_STATUS		0x40
#define MCDX_CMD_SET_DRIVE_MODE		0x50
#define MCDX_CMD_RESET			0x60
#define MCDX_CMD_HOLD			0x70
#define MCDX_CMD_CONFIG			0x90
#define MCDX_CMD_SET_ATTENATOR		0xae
#define MCDX_CMD_PLAY			0xc0
#define MCDX_CMD_PLAY_2X		0xc1
#define MCDX_CMD_GET_DRIVE_MODE		0xc2
#define MCDX_CMD_SET_INTERLEAVE		0xc8
#define MCDX_CMD_GET_FIRMWARE		0xdc
#define MCDX_CMD_SET_DATA_MODE		0xa0
#define MCDX_CMD_STOP			0xf0
#define MCDX_CMD_EJECT			0xf6
#define MCDX_CMD_CLOSE_DOOR		0xf8
#define MCDX_CMD_LOCK_DOOR		0xfe

#define READ_AHEAD			8	/* 16 Sectors (4K) */

#ifndef I_WAS_IN_MCDX_H
#warning You have not edited mcdx.h
#warning Perhaps irq and i/o settings are wrong.
#endif

#endif 	/* __MCDX_H */
