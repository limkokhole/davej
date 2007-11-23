/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP protocol.
 *
 * Version:	@(#)ip.h	1.0.2	04/28/93
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IP_H
#define _LINUX_IP_H


#define IPOPT_END	0
#define IPOPT_NOOP	1
#define IPOPT_SEC	130
#define IPOPT_LSRR	131
#define IPOPT_SSRR	137
#define IPOPT_RR	7
#define IPOPT_SID	136
#define IPOPT_TIMESTAMP	68


struct timestamp {
	u8	len;
	u8	ptr;
	union {
#if defined(__i386__)  
		u8	flags:4,
			overflow:4;
#elif defined(__mc68000__)
		u8	overflow:4,
			flags:4;
#elif defined(__alpha__)
		u8	flags:4,
			overflow:4;
#else
#error	"Adjust this structure to match your CPU"
#endif						
		u8	full_char;
	} x;
	u32	data[9];
};


#define MAX_ROUTE	16

struct route {
  char		route_size;
  char		pointer;
  unsigned long route[MAX_ROUTE];
};


struct options {
  struct route		record_route;
  struct route		loose_route;
  struct route		strict_route;
  struct timestamp	tstamp;
  unsigned short	security;
  unsigned short	compartment;
  unsigned short	handling;
  unsigned short	stream;
  unsigned		tcc;
};


struct iphdr {
#if defined(__i386__)
	u8	ihl:4,
		version:4;
#elif defined (__mc68000__)
	u8	version:4,
  		ihl:4;
#elif defined (__alpha__)
	u8	ihl:4,
		version:4;
#else
#error "Adjust this structure to match your CPU"
#endif
	u8	tos;
	u16	tot_len;
	u16	id;
	u16	frag_off;
	u8	ttl;
	u8	protocol;
	u16	check;
	u32	saddr;
	u32	daddr;
	/*The options start here. */
};


#endif	/* _LINUX_IP_H */
