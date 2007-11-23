/* $Id: sbuscons.c,v 1.7 1997/08/28 09:30:07 davem Exp $
 * sbuscons.c: Routines specific to SBUS frame buffer consoles.
 *
 * Copyright (C) 1995 Peter Zaitcev (zaitcev@lab.ipmce.su)
 * Copyright (C) 1995,1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995, 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 *
 * Added font loading Nov/21, Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Added render_screen and faster scrolling Nov/27, miguel
 * Added console palette code for cg6 Dec/13/95, miguel
 * Added generic frame buffer support Dec/14/95, miguel
 * Added cgsix and bwtwo drivers Jan/96, miguel
 * Added 4m, and cg3 driver Feb/96, miguel
 * Fixed the cursor on color displays Feb/96, miguel.
 * Cleaned up the detection code, generic 8bit depth display 
 *   code, Mar/96 miguel
 * Hacked support for cg14 video cards -- Apr/96, miguel.
 * Color support for cg14 video cards -- May/96, miguel.
 * Code split, Dave Redman, May/96
 * Be more VT change friendly, May/96, miguel.
 * Support for hw cursor and graphics acceleration, Jun/96, jj.
 * Added TurboGX+ detection (cgthree+), Aug/96, Iain Lea (iain@sbs.de)
 * Added TCX support (8/24bit), Aug/96, jj.
 * Support for multiple framebuffers, Sep/96, jj.
 * Fix bwtwo inversion and handle inverse monochrome cells in
 *   sun_blitc, Nov/96, ecd.
 * Fix sun_blitc and screen size on displays other than 1152x900, 
 *   128x54 chars, Nov/96, jj.
 * Fix cursor spots left on some non-accelerated fbs, changed
 *   software cursor to be like the hw one, Nov/96, jj.
 * 
 * Much of this driver is derived from the DEC TGA driver by
 * Jay Estabrook who has done a nice job with the console
 * driver abstraction btw.
 *
 * We try to make everything a power of two if possible to
 * speed up the bit blit.  Doing multiplies, divides, and
 * remainder routines end up calling software library routines
 * since not all Sparcs have the hardware to do it.
 *
 * TODO:
 * do not blank the screen when frame buffer is mapped.
 *
 */

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/proc_fs.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/fbio.h>
#include <asm/io.h>
#include <asm/smp.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/selection.h>
#include <linux/console_struct.h>

#include "fb.h"

#define cmapsz 8192

#include "suncons_font.h"

#define ASM_BLITC

int sbus_hw_cursor_shown = 0;

void sbus_hw_hide_cursor(void);
void sbus_hw_set_cursor(int,int);

static int sbus_blitc(uint, unsigned long);

static void sbus_install_consops(void);

extern void register_console(void (*proc)(const char *));
extern void console_print(const char *);
extern void putconsxy(int, char *);
extern unsigned char vga_font[];
extern int serial_console;
extern char *console_fb_path;

/* The following variables describe a Sparc console. */

/* Screen dimensions and color depth. */
static int con_depth, con_width, con_height, con_type;

/* Base address of first line. */
static unsigned char *con_fb_base;

/* Screen parameters: we compute those at startup to make the code faster */
static int chars_per_line;	/* number of bytes per line */
static int ints_per_line;	/* number of ints per  line */
static int ints_per_cursor;  	/* 14 * ints_per_line */
static int skip_bytes;		/* number of bytes we skip for the y margin + x_margin */
static int x_margin, y_margin;	/* the x and y margins */
static int bytes_per_row;	/* bytes used by one screen line (of 16 scan lines)  */
int sun_prom_console_id = 0;

/* Functions used by the SPARC dependent console code
 * to perform the fb_restore_palette function.
 */
extern void (*fb_restore_palette)(fbinfo_t *fbinfo);
static void sbus_set_palette (void);

 /* Our screen looks like at 1152 X 900:
 *
 *  0,0
 *      ------------------------------------------------------------------
 *      |                          ^^^^^^^^^^^                           |
 *      |                          18 y-pixels                           |
 *      |                          ^^^^^^^^^^^                           |
 *   13 | <-64 pixels->|  <-- 128 8x16 characters -->    | <-64 pixels-> |
 *    ....
 *                         54 chars from top to bottom
 *    ....
 *  888 | <-64 pixels->|  <-- 128 8x16 characters -->    | <-64 pixels-> |
 *      |                          ^^^^^^^^^^^                           |
 *      |                          18 y-pixels                           |
 *      |                          ^^^^^^^^^^^                           |
 *      ------------------------------------------------------------------
 */
/* First for MONO displays. */
#define SCREEN_WIDTH     1152     /* Screen width in pixels  */
#define SCREEN_HEIGHT    900      /* Screen height in pixels */
#define CHARS_PER_LINE   144      /* Make this empirical for speed */
#define NICE_Y_MARGIN    18       /* We skip 18 y-pixels at top/bottom */
#define NICE_X_MARGIN    8        /* We skip 64 x-pixels at left/right */
#define FBUF_TOP_SKIP    2592     /* Empirical, (CHARS_PER_LINE * NICE_Y_MARGIN) */
#define CHAR_HEIGHT      16
#define ONE_ROW          2304     /* CHARS_PER_LINE * CHAR_HEIGHT */

/* Now we have this, to compute the base frame buffer position
 * for a new character to be rendered. 1 and 8 bit depth.
 */
#define FBUF_OFFSET(cindex) \
        (((FBUF_TOP_SKIP) + (((cindex)>>7) * ONE_ROW)) + \
	 ((NICE_X_MARGIN) + (((cindex)&127))))


#define COLOR_FBUF_OFFSET(cindex) (*color_fbuf_offset)(cindex)

/* These four routines are optimizations for the _generic routine for
 * the most common cases.
 * I guess doing twice sll is much faster than doing .mul, sra faster
 * than doing .div, and the disadvantage that someone has to call it
 * (it cannot be inline) runs away, 'cause otherwise it would have to
 * call .mul anyway. 
 * The shifting + addition only routines won't eat any stack frame :))
 * Names come from width, screen_num_columns.
 */
static int color_fbuf_offset_1280_144 (int cindex)
{
	register int i = (cindex/144);
	/* (1280 * CHAR_HEIGHT) == 101.0000.0000.0000 */
	return skip_bytes + (i << 14) + (i << 12) + ((cindex % 144) << 3);
}

static int color_fbuf_offset_1152_128 (int cindex)
{
	register int i = (cindex>>7);
	/* (1152 * CHAR_HEIGHT) == 100.1000.0000.0000 */
	return skip_bytes + (i << 14) + (i << 11) + ((cindex & 127) << 3);
}

static int color_fbuf_offset_1024_128 (int cindex)
{
	register int i = (cindex>>7);
	/* (1024 * CHAR_HEIGHT) == 100.0000.0000.0000 */
	return skip_bytes + (i << 14) + ((cindex & 127) << 3);
}

static int color_fbuf_offset_800_96 (int cindex)
{
	register int i = (cindex / 96);
	/* (800 * CHAR_HEIGHT) == 11.0010.0000.0000 */
	return skip_bytes + (i<<13) + (i<<12) + (i<<9) + ((cindex % 96)<<3);
}

static int color_fbuf_offset_640_80 (int cindex)
{
	register int i = (cindex/80);
	/* (640 * CHAR_HEIGHT) == 10.1000.0000.0000 */
	return skip_bytes + (i << 13) + (i << 11) + ((cindex % 80) << 3);
}
	 
static int color_fbuf_offset_generic (int cindex)
{
	return skip_bytes + (cindex / video_num_columns) * bytes_per_row + ((cindex % video_num_columns) << 3);
}

static int (*color_fbuf_offset)(int) = color_fbuf_offset_generic;
	 
static int do_accel = 0;

/* For the cursor, we just invert the 8x16 block at the cursor
 * location.  Easy enough...
 *
 * Hide the cursor from view, during blanking, usually...
 */
static int cursor_pos = -1;

static unsigned int under_cursor[4];

static void sbus_hide_cursor(void)
{
	unsigned long flags;
	int j;

	if (fbinfo[0].setcursor) {
		sbus_hw_hide_cursor();
		return;
	}
	
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return; /* Don't paint anything on fb which is not ours,
			   but turn off the hw cursor in such case */
	
	__save_and_cli(flags);

	if(cursor_pos == -1) {
		__restore_flags (flags);
		return;
	}
	switch (con_depth){
	case 1: {
		unsigned char *dst;
		dst = (unsigned char *)((unsigned long)con_fb_base +
					FBUF_OFFSET(cursor_pos));
		for(j = 0; j < CHAR_HEIGHT; j++, dst += CHARS_PER_LINE)
			*dst = ~(*dst);
		break;
	}
	case 8: {
		unsigned int *dst;
		
		dst = (unsigned int *)((unsigned long)con_fb_base +
					COLOR_FBUF_OFFSET(cursor_pos)) + ints_per_cursor;
		dst[0] = under_cursor[0];
		dst[1] = under_cursor[1];
		dst[ints_per_line] = under_cursor[2];
		dst[ints_per_line+1] = under_cursor[3];
		break;
	}
	default:
		break;
	}
	cursor_pos = -1;
	__restore_flags(flags);
}

static void sbus_set_cursor(int currcons)
{
	int j, idx, oldpos;
	unsigned long flags;

	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;

	if (fbinfo[0].setcursor) {
		if (!deccm)
			sbus_hide_cursor();
		else {
			idx = (pos - video_mem_base) >> 1;
			
			sbus_hw_set_cursor(x_margin + ((idx % video_num_columns) << 3), y_margin + ((idx / video_num_columns) * CHAR_HEIGHT));
		}
		return;
	}

	__save_and_cli(flags);

	idx = (pos - video_mem_base) >> 1;
	oldpos = cursor_pos;
	if (!deccm) {
		sbus_hide_cursor ();
		__restore_flags (flags);
		return;
	}
	cursor_pos = idx;
	switch (con_depth){
	case 1: {
		unsigned char *dst, *opos;
		
		dst = (unsigned char *)((unsigned long)con_fb_base + FBUF_OFFSET(idx));
		opos = (unsigned char *)((unsigned long)con_fb_base + FBUF_OFFSET(oldpos));
		if(oldpos != -1) {
			/* Restore what was at the old position */
			for(j=0; j < CHAR_HEIGHT; j++, opos += CHARS_PER_LINE) {
				*opos = ~*opos;
			}
		}
		for(j=0; j < 16; j++, dst+=CHARS_PER_LINE) {
			*dst = ~*dst;
		}
		break;
	}
	case 8: {
		unsigned int *dst, *opos;
		dst = (unsigned int *)((unsigned long)con_fb_base + COLOR_FBUF_OFFSET(idx)) + ints_per_cursor;
			
		if(oldpos != -1) {
			opos = (unsigned int *)((unsigned long)con_fb_base + COLOR_FBUF_OFFSET(oldpos)) + ints_per_cursor;
			opos[0] = under_cursor[0];
			opos[1] = under_cursor[1];
			opos[ints_per_line] = under_cursor[2];
			opos[ints_per_line+1] = under_cursor[3];
		}
		under_cursor[0] = dst[0];
		under_cursor[1] = dst[1];
		under_cursor[2] = dst[ints_per_line];
		under_cursor[3] = dst[ints_per_line+1];
		dst[0] = 0x00000000;
		dst[1] = 0x00000000;
		dst[ints_per_line] = 0x00000000;
		dst[ints_per_line+1] = 0x00000000;
		break;
	}
	default:
	}
	__restore_flags(flags);
}

/*
 * Render the current screen
 * Only used at startup and when switching from KD_GRAPHICS to KD_TEXT
 * to avoid the caching that is being done in selection.h
 */

static void sbus_render_screen(void)
{
    int count;
    unsigned short *contents;

    count = video_num_columns * video_num_lines;
    contents = (unsigned short *) video_mem_base;

    for (;count--; contents++)
	sbus_blitc (*contents, (unsigned long) contents);
}

__initfunc(static unsigned long
sbus_con_type_init(unsigned long kmem_start, const char **display_desc))
{
        can_do_color = (con_type != FBTYPE_SUN2BW);

        video_type = VIDEO_TYPE_SUN;
        *display_desc = "SUN";

	if (!serial_console) {
		/* If we fall back to PROM then our output have to remain readable. */
		prom_putchar('\033');  prom_putchar('[');  prom_putchar('H');

		/*
		 * fake the screen memory with some CPU memory
		 */
		video_mem_base = kmem_start;
		kmem_start += video_screen_size;
		video_mem_term = kmem_start;
	}
	return kmem_start;
}

__initfunc(static void sbus_con_type_init_finish(void))
{
	int i, cpu;
	char *p = con_fb_base + skip_bytes;
	char q[2] = {0,5};
	int currcons = 0;
	unsigned short *ush;
	int ncpus;

	if (serial_console)
		return;
	ncpus = linux_num_cpus;
	if (ncpus > 4) ncpus = 4;
	if (fbinfo[0].draw_penguin) {
		(*fbinfo[0].draw_penguin)(x_margin, y_margin, ncpus);
	} else if (con_depth == 8 && fbinfo[0].loadcmap) {
		for (i = 0; i < linux_logo_colors; i++) {
			fbinfo[0].color_map CM(i+32,0) = linux_logo_red [i];
			fbinfo[0].color_map CM(i+32,1) = linux_logo_green [i];
			fbinfo[0].color_map CM(i+32,2) = linux_logo_blue [i];
		}
		(*fbinfo [0].loadcmap)(&fbinfo [0], 0, linux_logo_colors + 32);
		for (i = 0; i < 80; i++, p += chars_per_line){
		        for (cpu = 0; cpu < ncpus; cpu++){
				memcpy (p + (cpu * 88), linux_logo + 80 * i, 80);
			}
		}
	} else if (con_depth == 1) {
		for (i = 0; i < 80; i++, p += chars_per_line)
			memcpy (p, linux_logo_bw + 10 * i, 10);
	}
	putconsxy(0, q);
	ush = (unsigned short *) video_mem_base + video_num_columns * 2 + 20 + 11 * (ncpus - 1);

	p = logo_banner;
	for (; *p; p++, ush++) {
		*ush = (attr << 8) + *p;
		sbus_blitc (*ush, (unsigned long) ush);
	}
	for (i = 0; i < 5; i++) {
		ush = (unsigned short *) video_mem_base + i * video_num_columns;
		memset (ush, 0, 20);
	}
}

/*
 * NOTE: get_scrmem() and set_scrmem() are here only because
 * the VGA version of set_scrmem() has some direct VGA references.
 */
static void sbus_get_scrmem(int currcons)
{
	memcpyw((unsigned short *)vc_scrbuf[currcons],
		(unsigned short *)origin, video_screen_size);
	origin = video_mem_start = (unsigned long)vc_scrbuf[currcons];
	scr_end = video_mem_end = video_mem_start + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
}

static void sbus_set_scrmem(int currcons, long offset)
{
	if (video_mem_term - video_mem_base < offset + video_screen_size)
		offset = 0;
	memcpyw((unsigned short *)(video_mem_base + offset),
		(unsigned short *) origin, video_screen_size);
	video_mem_start = video_mem_base;
	video_mem_end = video_mem_term;
	origin = video_mem_base + offset;
	scr_end = origin + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
}

/*
 * PIO_FONT support.
 */
static int sbus_set_get_font(char * arg, int set, int ch512)
{
	int i, line;

	if (!arg)
		return -EINVAL;

	/* download the current font */
	if (!set){
		if(clear_user(arg, cmapsz))
			return -EFAULT;
		for (i = 0; i < 256; i++) {
			for (line = 0; line < CHAR_HEIGHT; line++) {
				unsigned char value = vga_font[i];

				/* Access checked by the above clear_user */
				__put_user_ret (value, (arg + (i * 32 + line)),
						-EFAULT);
			}
		}
		return 0;
	}
	
        /* set the font */
        
        if (verify_area (VERIFY_READ, arg, 256 * CHAR_HEIGHT)) return -EFAULT;
	for (i = 0; i < 256; i++) {
		for (line = 0; line < CHAR_HEIGHT; line++){
			unsigned char value;
			__get_user_ret(value, (arg + (i * 32 + line)),-EFAULT);
			vga_font [i*CHAR_HEIGHT + line] = value;
		}
	}
	return 0;
}

/*
 * Adjust the screen to fit a font of a certain height
 *
 * Returns < 0 for error, 0 if nothing changed, and the number
 * of lines on the adjusted console if changed.
 *
 * for now, we only support the built-in font...
 */
static int sbus_con_adjust_height(unsigned long fontheight)
{
	return -EINVAL;
}

static int sbus_set_get_cmap(unsigned char * arg, int set)
{
	int i;

	if(set)
		i = VERIFY_READ;
	else
		i = VERIFY_WRITE;
	if(verify_area(i, arg, (16 * 3 * sizeof(unsigned char))))
		return -EFAULT;
	for (i=0; i<16; i++) {
		if (set) {
			__get_user_ret(default_red[i], (arg+0),-EFAULT);
			__get_user_ret(default_grn[i], (arg+1),-EFAULT);
			__get_user_ret(default_blu[i], (arg+2),-EFAULT);
		} else {
			__put_user_ret(default_red[i], (arg+0),-EFAULT);
			__put_user_ret(default_grn[i], (arg+1),-EFAULT);
			__put_user_ret(default_blu[i], (arg+2),-EFAULT);
		}
		arg += 3;
	}
	if (set) {
		for (i=0; i<MAX_NR_CONSOLES; i++)
			if (vc_cons_allocated(i)) {
				int j, k ;
				for (j=k=0; j<16; j++) {
					vc_cons[i].d->vc_palette[k++] = default_red[j];
					vc_cons[i].d->vc_palette[k++] = default_grn[j];
					vc_cons[i].d->vc_palette[k++] = default_blu[j];
				}
			}
		sbus_set_palette();
	}

	return 0;
}

static void sbus_clear_screen(void)
{
	if (fbinfo[0].fill) {
		int rects [4];
		
		rects [0] = 0;
		rects [1] = 0;
		rects [2] = con_width;
		rects [3] = con_height;
		(*fbinfo[0].fill)(reverse_color_table[0], 1, rects);
	} else if (fbinfo[0].base && fbinfo[0].base_depth)
		memset (con_fb_base,
			(con_depth == 1) ? ~(0) : reverse_color_table[0],
			(con_depth * con_height * con_width) / 8);
	/* also clear out the "shadow" screen memory */
	memset((char *)video_mem_base, 0, (video_mem_term - video_mem_base));
	cursor_pos = -1;
}

static void sbus_clear_fb(int n)
{
	if (!n) {
		sbus_clear_screen ();
	} else if (fbinfo[n].base && fbinfo[n].base_depth) {
		memset((void *)fbinfo[n].base,
		       (fbinfo[n].base_depth == 1) ?
			~(0) : reverse_color_table[0],
		       (fbinfo[n].base_depth * fbinfo[n].type.fb_height
					     * fbinfo[n].type.fb_width) / 8);
	}
}

static void sbus_clear_margin(void)
{
	int h, he, i;
	unsigned char *p;

	if (fbinfo[0].fill) {
		int rects [16];

		rects [0] = 0;
		rects [1] = 0;
		rects [2] = con_width;
		rects [3] = y_margin;
		rects [4] = 0;
		rects [5] = y_margin;
		rects [6] = x_margin;
		rects [7] = con_height;
		rects [8] = con_width - x_margin;
		rects [9] = y_margin;
		rects [10] = con_width;
		rects [11] = con_height;
		rects [12] = x_margin;
		rects [13] = con_height - y_margin;
		rects [14] = con_width - x_margin;
		rects [15] = con_height;
		(*fbinfo[0].fill)(reverse_color_table[0], 4, rects);
	} else {
		memset (con_fb_base, 
			(con_depth == 1) ? ~(0) : reverse_color_table[0],
			skip_bytes - (x_margin<<1));
		memset (con_fb_base + chars_per_line * con_height
					- skip_bytes + (x_margin<<1),
			(con_depth == 1) ? ~(0) : reverse_color_table[0],
			skip_bytes - (x_margin<<1));
		he = con_height - 2 * y_margin;
		i = 2 * x_margin;
		if (con_depth == 1) {
			for (p = con_fb_base+skip_bytes-(x_margin<<1), h = 0;
			     h <= he; p += chars_per_line, h++)
				memset (p, ~(0), i);			
		} else {
			for (p = con_fb_base+skip_bytes-(x_margin<<1), h = 0;
			     h <= he; p += chars_per_line, h++)
				memset (p, reverse_color_table[0], i);
		}
	}
	if (fbinfo [0].switch_from_graph)
		(*fbinfo [0].switch_from_graph)();
}

/* Call the frame buffer routine for setting the palette */
static void sbus_set_palette (void)
{
	if (console_blanked || vt_cons [fg_console]->vc_mode == KD_GRAPHICS)
		return;

	if (fbinfo [0].loadcmap){
		int i, j;
	
		/* First keep color_map with the palette colors */
		for (i = 0; i < 16; i++){
			j = sparc_color_table [i];
			fbinfo[0].color_map CM(i,0) = default_red [j];
			fbinfo[0].color_map CM(i,1) = default_grn [j];
			fbinfo[0].color_map CM(i,2) = default_blu [j];
		}
		(*fbinfo [0].loadcmap)(&fbinfo [0], 0, 16);
	}
}

static void sbus_set_other_palette (int n)
{
	if (!n) {
		sbus_set_palette ();
		return;
	}
	if (fbinfo [n].loadcmap){
		fbinfo[n].color_map CM(0,0) = 0;
		fbinfo[n].color_map CM(0,1) = 0;
		fbinfo[n].color_map CM(0,2) = 0;
		(*fbinfo [n].loadcmap)(&fbinfo [n], 0, 1);
	}
}

/* Called when returning to prom */
static void sbus_console_restore_palette (void)
{
        if (fb_restore_palette)
	        (*fb_restore_palette) (&fbinfo[0]);
}

__initfunc(unsigned long cg_postsetup(fbinfo_t *fb, unsigned long start_mem))
{
	fb->color_map = (char *)start_mem;
	return start_mem + 256*3;
}

static char *known_cards [] __initdata = {
	"cgsix", "cgthree", "cgRDI", "cgthree+", "bwtwo", "SUNW,tcx",
	"cgfourteen", "SUNW,leo", "SUNW,ffb", 0
};
static char *v0_known_cards [] __initdata = {
	"cgsix", "cgthree", "cgRDI", "cgthree+", "bwtwo", 0
};

__initfunc(static int known_card (char *name, char **known_cards))
{
	int i;

	for (i = 0; known_cards [i]; i++)
		if (strcmp (name, known_cards [i]) == 0)
			return 1;
	return 0;
}

static struct {
	int depth;
	int resx, resy;
	int x_margin, y_margin;
} scr_def [] = {
	{ 8, 1280, 1024, 64, 80 },
	{ 8, 1152, 1024, 64, 80 },
	{ 8, 1152, 900,  64, 18 },
	{ 8, 1024, 768,  0,  0 },
	{ 8, 800, 600, 16, 12 },
	{ 8, 640, 480, 0, 0 },
	{ 1, 1152, 900,  8,  18 },
	{ 0 },
};

__initfunc(static int cg14_present(void))
{
	int root, n;

	root = prom_getchild (prom_root_node);
	if ((n = prom_searchsiblings (root, "obio")) == 0)
		return 0;

	n = prom_getchild (n);
	if ((n = prom_searchsiblings (n, "cgfourteen")) == 0)
		return 0;
	return n;
}

__initfunc(static int creator_present (void))
{
#ifdef __sparc_v9__
	int root, n;

	root = prom_getchild (prom_root_node);
	if ((n = prom_searchsiblings (root, "SUNW,ffb")) == 0)
		return 0;
	return n;
#else
	return 0;
#endif
}

__initfunc(static void
	   sparc_framebuffer_setup(int primary, int con_node,
				   int type, struct linux_sbus_device *sbdp, 
				   uint base, unsigned long con_base, int prom_fb,
				   int parent_node))
{
	static int frame_buffers = 1;
	int n, i;
	int linebytes;
	uint io = 0;
	char *p;
	
	if (primary)
		n = 0;
	else {
		if (frame_buffers == FRAME_BUFFERS)
			return; /* Silently ignore */
		n = frame_buffers++;
	}
	
	if (prom_fb) sun_prom_console_id = n;
		
	if (sbdp)
		io = sbdp->reg_addrs [0].which_io;

	/* Fill in common fb information */
	fbinfo [n].clear_fb       = sbus_clear_fb;
	fbinfo [n].set_other_palette = sbus_set_other_palette;
	fbinfo [n].type.fb_type   = type;
	fbinfo [n].real_type	  = type;
	fbinfo [n].prom_node	  = con_node;
	memset (&(fbinfo [n].emulations), 0xff, sizeof (fbinfo [n].emulations));
	fbinfo [n].type.fb_height = prom_getintdefault(con_node, "height", 900);
	fbinfo [n].type.fb_width  = prom_getintdefault(con_node, "width", 1152);
	fbinfo [n].type.fb_depth  = (type == FBTYPE_SUN2BW) ? 1 : 8;
	linebytes = prom_getint(con_node, "linebytes");
	if (linebytes == -1) linebytes = fbinfo [n].type.fb_width;
	fbinfo [n].type.fb_size   = PAGE_ALIGN((linebytes) * (fbinfo [n].type.fb_height));
	fbinfo [n].space = io;
	fbinfo [n].blanked = 0;
	if (con_base >= PAGE_OFFSET)
		fbinfo [n].base = con_base;
	else
		fbinfo [n].base = 0;
	fbinfo [n].cursor.hwsize.fbx = 32;
	fbinfo [n].cursor.hwsize.fby = 32;
	fbinfo [n].proc_entry.node = parent_node;
	fbinfo [n].proc_entry.rdev = MKDEV(GRAPHDEV_MAJOR, n);
	fbinfo [n].proc_entry.mode = S_IFCHR | S_IRUSR | S_IWUSR;
	prom_getname (con_node, fbinfo [n].proc_entry.name, 32 - 3);
	p = strchr (fbinfo [n].proc_entry.name, 0);
        sprintf (p, ":%d", n);
	
	/* Should be filled in for supported video cards */
	fbinfo [n].mmap = 0; 
	fbinfo [n].loadcmap = 0;
	fbinfo [n].ioctl = 0;
	fbinfo [n].reset = 0;
	fbinfo [n].blank = 0;
	fbinfo [n].unblank = 0;
	fbinfo [n].setcursor = 0;
	fbinfo [n].base_depth = fbinfo [n].type.fb_depth;
	
	/* Per card setup */
	switch (fbinfo [n].type.fb_type){
#ifdef SUN_FB_CGTHREE
	case FBTYPE_SUN3COLOR:
		cg3_setup (&fbinfo [n], n, base, io, sbdp);
		break;
#endif
#ifdef SUN_FB_TCX
	case FBTYPE_TCXCOLOR:
		tcx_setup (&fbinfo [n], n, con_node, base, sbdp);
		break;
#endif
#ifdef SUN_FB_CGSIX
	case FBTYPE_SUNFAST_COLOR:
		cg6_setup (&fbinfo [n], n, base, io);
		break;
#endif
#ifdef SUN_FB_BWTWO
	case FBTYPE_SUN2BW:
		bwtwo_setup (&fbinfo [n], n, base, io, sbdp);
		break;
#endif
#ifdef SUN_FB_CGFOURTEEN
	case FBTYPE_MDICOLOR:
		cg14_setup (&fbinfo [n], n, con_node, base, io);
		break;
#endif
#ifdef SUN_FB_LEO
	case FBTYPE_SUNLEO:
		leo_setup (&fbinfo [n], n, base, io);
		break;
#endif
#if defined(SUN_FB_CREATOR) && defined(__sparc_v9__)
	case FBTYPE_CREATOR:
		creator_setup (&fbinfo [n], n, con_node, base, io);
		break;
#endif
	default:
		fbinfo [n].type.fb_type = FBTYPE_NOTYPE;
		return;
	}

	if (n)
		return;
	
	/* Code below here is just executed for the first frame buffer */
	con_type = type;
	con_height = fbinfo [n].type.fb_height;
	con_width = fbinfo [n].type.fb_width;
	con_depth = (type == FBTYPE_SUN2BW) ? 1 : 8;
	for (i = 0; scr_def [i].depth; i++){
		if ((scr_def [i].resx != con_width) ||
		    (scr_def [i].resy != con_height))
			continue;
		if (scr_def [i].depth != con_depth)
			continue;
		x_margin = scr_def [i].x_margin;
		y_margin = scr_def [i].y_margin;
		chars_per_line = (con_width * con_depth) / 8;
		skip_bytes = chars_per_line * y_margin + x_margin;
		ints_per_line = chars_per_line / 4;
		ints_per_cursor = 14 * ints_per_line;
		bytes_per_row = CHAR_HEIGHT * chars_per_line;
		ORIG_VIDEO_COLS = con_width / 8 -
			2 * x_margin / con_depth;
		ORIG_VIDEO_LINES = (con_height - 2 * y_margin) / 16;
		switch (chars_per_line) {
		case 1280:
			if (ORIG_VIDEO_COLS == 144)
				color_fbuf_offset =
					color_fbuf_offset_1280_144;
			break;
		case 1152:
			if (ORIG_VIDEO_COLS == 128)
				color_fbuf_offset =
					color_fbuf_offset_1152_128;
			break;
		case 1024:
			if (ORIG_VIDEO_COLS == 128)
				color_fbuf_offset =
					color_fbuf_offset_1024_128;
			break;
		case 800:
			if (ORIG_VIDEO_COLS == 96)
				color_fbuf_offset =
					color_fbuf_offset_800_96;
			break;
		case 640:
			if (ORIG_VIDEO_COLS == 80)
				color_fbuf_offset =
					color_fbuf_offset_640_80;
			break;
		}
		break;
	}
	
	if (!scr_def [i].depth){
		x_margin = y_margin = 0;
		prom_printf ("console: unknown video resolution %dx%d,"
			     " depth %d\n",
			     con_width, con_height, con_depth);
		prom_halt ();
	}
	
	/* P3: I fear this strips 15inch 1024/768 PC-like
	 * monitors out. */
	if ((linebytes*8) / con_depth != con_width) {
		prom_printf("console: unusual video, linebytes=%d, "
			    "width=%d, height=%d depth=%d\n",
			    linebytes, con_width, con_height,
			    con_depth);
		prom_halt ();
	}
}

__initfunc(int sbus_console_probe(void))
{
	int propl, con_node, default_node = 0;
	char prop[16];
	struct linux_sbus_device *sbdp, *sbdprom;
	struct linux_sbus *sbus;
	int creator = 0, cg14 = 0;
	char prom_name[40];	
	int type, card_found = 0;
	unsigned long con_base;
	u32 tmp;
	u32 prom_console_node = 0;

	if(SBus_chain == 0)
		return -1;

	sbdprom = 0;
	switch(prom_vers) {
	case PROM_V0:
		/* V0 proms are at sun4c only. Can skip many checks. */
		con_type = FBTYPE_NOTYPE;
		for_each_sbusdev(sbdp, SBus_chain) {
			/* If no "address" than it is not the PROM console. */
			if(sbdp->num_vaddrs) {
				if(known_card(sbdp->prom_name, v0_known_cards)) {
				   	sbdprom = sbdp;
					strncpy(prom_name, sbdp->prom_name, sizeof (prom_name));
					break;
				}
			}
		}
		if(!sbdprom) return -1;
		for_each_sbusdev(sbdp, SBus_chain) {
			con_node = sbdp->prom_node;

			if(!strncmp(sbdp->prom_name, "cgsix", 5) ||
			   !strncmp(sbdp->prom_name, "cgthree+", 8)) {
				type = FBTYPE_SUNFAST_COLOR;
			} else if(!strncmp(sbdp->prom_name, "cgthree", 7) ||
			   !strncmp(sbdp->prom_name, "cgRDI", 5)) {
				type = FBTYPE_SUN3COLOR;
			} else if (!strncmp(sbdp->prom_name, "bwtwo", 5)) {
				type = FBTYPE_SUN2BW;
			} else
				continue;
			sparc_framebuffer_setup (sbdprom == sbdp, con_node, type, sbdp, 
						 (uint)sbdp->reg_addrs [0].phys_addr, sbdp->sbus_vaddrs[0], 0,
						 sbdp->my_bus->prom_node);
			/* XXX HACK */
			if (sbdprom == sbdp && !strncmp(sbdp->prom_name, "cgRDI", 5))
				break;
		}
		break;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		if (console_fb_path) {
			char *q, c;

			for (q = console_fb_path; *q && *q != ' '; q++);
			c = *q;
			*q = 0;
			default_node = prom_pathtoinode(console_fb_path);
			if (default_node) {
				prom_printf ("Using %s for console\n", console_fb_path);
				prom_console_node = prom_inst2pkg(prom_stdout);
				if (prom_console_node == default_node)
					prom_console_node = 0;
			}
		}
		if (!default_node)
			default_node = prom_inst2pkg(prom_stdout);
		propl = prom_getproperty(default_node, "device_type",
					 prop, sizeof (prop));
		if (propl < 0) {
			prom_printf ("output-device doesn't have device_type property\n");
			prom_halt ();
		} else if (propl != sizeof("display") || strncmp("display", prop, sizeof("display"))) {
		    	prop [propl] = 0;
			prom_printf ("console_probe: output-device is %s"
				     " (not \"display\")\n", prop);
			prom_halt ();
		}
		for_all_sbusdev(sbdp, sbus) {
			if ((sbdp->prom_node == default_node)
			    && known_card (sbdp->prom_name, known_cards)) {
			    	sbdprom = sbdp;
				break;
			}
		}
		if (sbdprom)
		    card_found = 1;
		if (!card_found)
		    card_found = cg14 = cg14_present ();
		if (!card_found){
			card_found = creator = creator_present ();
		}
		if (!card_found){
			prom_printf ("Could not find a known video card on this machine\n");
			prom_halt ();
		}
		
		for_all_sbusdev(sbdp, sbus) {
			if (!known_card (sbdp->prom_name, known_cards))
				continue;
			con_node = sbdp->prom_node;
			prom_apply_sbus_ranges (sbdp->my_bus, &sbdp->reg_addrs [0],
						sbdp->num_registers, sbdp);

			propl = prom_getproperty(con_node, "address", (char *) &tmp, 4);
			con_base = tmp;
			if (propl != 4) con_base = 0;
			propl = prom_getproperty(con_node, "emulation", prom_name, sizeof (prom_name));
			if (propl < 0 || propl >= sizeof (prom_name)) {
				/* Early cg3s had no "emulation". */
				propl = prom_getproperty(con_node, "name", prom_name, sizeof (prom_name));
				if (propl < 0) {
					prom_printf("console: no device name!!\n");
					return -1;
				}
			}
			prom_name [sizeof (prom_name) - 1] = 0;
			if(!strcmp(prom_name, "cgsix") ||
			   !strcmp(prom_name, "cgthree+")) {
				type = FBTYPE_SUNFAST_COLOR;
			} else if(!strcmp(prom_name, "cgthree") ||
			   !strcmp(prom_name, "cgRDI")) {
				type = FBTYPE_SUN3COLOR;
			} else if(!strcmp(prom_name, "cgfourteen")) {
				type = FBTYPE_MDICOLOR;
			} else if(!strcmp(prom_name, "SUNW,leo")) {
				type = FBTYPE_SUNLEO;
			} else if(!strcmp(prom_name, "bwtwo")) {
				type = FBTYPE_SUN2BW;
			} else if(!strcmp(prom_name,"SUNW,tcx")){
				sparc_framebuffer_setup (sbdprom == sbdp, con_node, FBTYPE_TCXCOLOR, sbdp,
							 (uint)sbdp->reg_addrs [10].phys_addr, con_base, 
							 prom_console_node == con_node, sbdp->my_bus->prom_node);
				continue;
			} else {
				prom_printf("console: \"%s\" is unsupported\n", prom_name);
				continue;
			}
			sparc_framebuffer_setup (sbdprom == sbdp, con_node, type, sbdp,
						 (uint)sbdp->reg_addrs [0].phys_addr, con_base,
						 prom_console_node == con_node, sbdp->my_bus->prom_node);
			/* XXX HACK */
			if (sbdprom == sbdp && !strncmp(sbdp->prom_name, "cgRDI", 5))
				break;
		}
		if (cg14) {
			sparc_framebuffer_setup (!sbdprom, cg14, FBTYPE_MDICOLOR, 
						 0, 0, 0, prom_console_node == cg14,
						 prom_searchsiblings (prom_getchild (prom_root_node), "obio"));
		}
		if (creator){
			sparc_framebuffer_setup (!sbdprom, creator, FBTYPE_CREATOR,
						 0, 0, 0, prom_console_node == creator,
						 prom_root_node);
		}
		break;
	default:
		return -1;
	}
	
	if (fbinfo [0].type.fb_type == FBTYPE_NOTYPE) {
		prom_printf ("Couldn't setup your primary frame buffer.\n");
		prom_halt ();
	}

	if (fbinfo [0].blitc)
		do_accel = 1;
	
	con_fb_base = (unsigned char *)fbinfo[0].base;
	if (!con_fb_base){
		prom_printf ("PROM does not have an 'address' property for this\n"
			     "frame buffer and the Linux drivers do not know how\n"
			     "to map the video of this device\n");
		prom_halt ();
	}
	sbus_install_consops();
	return fb_init ();
}

/*
 * sbus_blitc
 *
 * Displays an ASCII character at a specified character cell
 *  position.
 *
 * Called from scr_writew() when the destination is
 *  the "shadow" screen
 */
static uint
fontmask_bits[16] = {
    0x00000000,
    0x000000ff,
    0x0000ff00,
    0x0000ffff,
    0x00ff0000,
    0x00ff00ff,
    0x00ffff00,
    0x00ffffff,
    0xff000000,
    0xff0000ff,
    0xff00ff00,
    0xff00ffff,
    0xffff0000,
    0xffff00ff,
    0xffffff00,
    0xffffffff
};

static int sbus_blitc(uint charattr, unsigned long addr)
{
	unsigned int fgmask, bgmask;
	unsigned char attrib;
	int j, idx;
	unsigned char *font_row;

	if (do_accel) {
		(*fbinfo[0].blitc)(charattr, 
			        x_margin + (((addr - video_mem_base) % video_size_row)<<2),
		 	        y_margin + CHAR_HEIGHT * ((addr - video_mem_base) / video_size_row));
		return 0;
	}

  	/* Invalidate the cursor position if necessary. */
	idx = (addr - video_mem_base) >> 1;

	attrib = CHARATTR_TO_SUNCOLOR(charattr);
	font_row = &vga_font[(j = (charattr & 0xff)) << 4];

	switch (con_depth){
	case 1: {
		register unsigned char *dst;
		unsigned long flags;
		
		dst = (unsigned char *)(((unsigned long)con_fb_base) + FBUF_OFFSET(idx));

		__save_and_cli(flags);
		if ((!(charattr & 0xf000)) ^ (idx == cursor_pos)) {
			for(j = 0; j < CHAR_HEIGHT; j++, font_row++, dst+=CHARS_PER_LINE)
				*dst = ~(*font_row);
		} else {
			for(j = 0; j < CHAR_HEIGHT; j++, font_row++, dst+=CHARS_PER_LINE)
				*dst = *font_row;
		}
		__restore_flags(flags);
		break;
	}
	case 8: {
#ifdef ASM_BLITC		
		const int cpl = chars_per_line;
		/* The register assignment is important here, do not modify without touching the assembly code as well */
		register unsigned int x1 __asm__("g4"), x2 __asm__("g5"), x3 __asm__("g2"), x4 __asm__("g3"), flags __asm__("g7");
		register unsigned int *dst __asm__("g1");
#else		
		const int ipl = ints_per_line;
		unsigned int data2, data3, data4;
		unsigned int data, rowbits;
		register unsigned int *dst;
		unsigned long flags;
#endif		
		const uint *fontm_bits = fontmask_bits;
		
		dst = (unsigned int *)(((unsigned long)con_fb_base) + COLOR_FBUF_OFFSET(idx));
		if (j == ' ') /* space is quite common, so we optimize a bit */ {
#ifdef ASM_BLITC
#define BLITC_SPACE \
		"\n\t std	%%g4, [%%g1]" \
		"\n\t std	%%g4, [%%g1 + %0]" \
		"\n\t add	%%g1, %1, %%g1"
#define BLITC_SPC \
		"\n\t std	%0, [%1]" \
		"\n\t std	%0, [%1 + %2]"

			x1 = attrib >> 4;
			x1 |= x1 << 8;
			x1 |= x1 << 16;
			x3 = cpl << 1;
			
			__asm__ __volatile__ (
				"\n\t mov	%2, %3"
				BLITC_SPACE
				BLITC_SPACE
				BLITC_SPACE
				BLITC_SPACE
				BLITC_SPACE
				BLITC_SPACE
				BLITC_SPACE
					: : "r" (cpl), "r" (x3), "r" (x1), "r" (x2));
			__save_and_cli (flags);
			if (idx != cursor_pos)
				__asm__ __volatile__ (BLITC_SPC : : "r" (x1), "r" (dst), "r" (cpl));
			else
				__asm__ __volatile__ (BLITC_SPC : : "r" (x1), "r" (under_cursor), "i" (8));
			__restore_flags (flags);
#else
			bgmask = attrib >> 4;
			bgmask |= bgmask << 8;
			bgmask |= bgmask << 16;
			
	                for(j = 0; j < CHAR_HEIGHT - 2; j++, font_row++, dst += ipl) {
        	                *dst = bgmask;
        	                *(dst+1) = bgmask;
                	}
                	/* Prevent cursor spots left on the screen */
			__save_and_cli(flags);
			if (idx != cursor_pos) {
	                	*dst = bgmask;
        	        	*(dst+1) = bgmask;
                		dst += ipl;
	                	*dst = bgmask;
        	        	*(dst+1) = bgmask;
                	} else {
	                	under_cursor [0] = bgmask;
        	        	under_cursor [1] = bgmask;
                		under_cursor [2] = bgmask;
	                	under_cursor [3] = bgmask;
        	        }
	                __restore_flags(flags);
#endif
		} else /* non-space */ {
			fgmask = attrib & 0x0f;
			bgmask = attrib >> 4;
			fgmask |= fgmask << 8;
			fgmask |= fgmask << 16;
			bgmask |= bgmask << 8;
			bgmask |= bgmask << 16;

#ifdef ASM_BLITC
#define BLITC_INIT \
			"\n\t ld	[%0], %%g2"
#define BLITC_BODY(ST1,SC1,ST2,SC2)  \
			"\n\t " #ST1 "	%%g2, " #SC1 ", %%g7"  \
			"\n\t " #ST2 "	%%g2, " #SC2 ", %7"  \
			"\n\t and	%%g7, 0x3c, %%g7"  \
			"\n\t and	%7, 0x3c, %7"  \
			"\n\t ld	[%1 + %%g7], %6"  \
			"\n\t and	%6, %2, %%g7"  \
			"\n\t andn	%3, %6, %6"  \
			"\n\t or	%%g7, %6, %6"  \
			"\n\t ld	[%1 + %7], %7"  \
			"\n\t and	%7, %2, %%g7"  \
			"\n\t andn	%3, %7, %7"  \
			"\n\t or	%%g7, %7, %7"
#define BLITC_BODYEND \
			"\n\t sll	%3, 2, %%g7"  \
			"\n\t srl	%3, 2, %3"  \
			"\n\t and	%%g7, 0x3c, %%g7"  \
			"\n\t and	%3, 0x3c, %3"  \
			"\n\t ld	[%0 + %%g7], %4"  \
			"\n\t and	%4, %1, %%g7"  \
			"\n\t andn	%2, %4, %4"  \
			"\n\t or	%%g7, %4, %4"  \
			"\n\t ld	[%0 + %3], %3"  \
			"\n\t and	%3, %1, %%g7"  \
			"\n\t andn	%2, %3, %3"  \
			"\n\t or	%%g7, %3, %3"
#define BLITC_STOREIT \
			"\n\t std	%6, [%5]"  \
			"\n\t add	%5, %4, %5"  \
			"\n\t" 
#define BLITC_STORE \
			"\n\t std	%%g4, [%0]"  \
			"\n\t std	%%g2, [%0 + %1]"
	
			for (j = 0; j < 3; j++, font_row+=4) {
				__asm__ __volatile__ (BLITC_INIT
					BLITC_BODY(srl, 26, srl, 22)
					BLITC_STOREIT
					BLITC_BODY(srl, 18, srl, 14)
					BLITC_STOREIT
					BLITC_BODY(srl, 10, srl, 6)
					BLITC_STOREIT
					BLITC_BODY(srl, 2, sll, 2)
					BLITC_STOREIT
					: : "r" (font_row), "r" (fontm_bits), "r" (fgmask), "r" (bgmask), "r" (cpl), "r" (dst),
					    "r" (x1), "r" (x2));
			}
			__asm__ __volatile__ (BLITC_INIT
				BLITC_BODY(srl, 26, srl, 22)
				BLITC_STOREIT
				BLITC_BODY(srl, 18, srl, 14)
				BLITC_STOREIT
				/* Now prepare date for the 15th line, but don't put it anywhere yet (leave it in g4,g5) */
				BLITC_BODY(srl, 10, srl, 6)
				: : "r" (font_row), "r" (fontm_bits), "r" (fgmask), "r" (bgmask), "r" (cpl), "r" (dst),
				    "r" (x1), "r" (x2));
			/* Prepare the data the bottom line (and put it into g2,g3) */
			__asm__ __volatile__ (BLITC_BODYEND : : "r" (fontm_bits), "r" (fgmask), "r" (bgmask),
								"r" (x3), "r" (x4));
			__save_and_cli(flags);
			if (idx != cursor_pos)
				__asm__ __volatile__ (BLITC_STORE : : "r" (dst), "r" (cpl));
			else
				__asm__ __volatile__ (BLITC_STORE : : "r" (under_cursor), "i" (8));
			__restore_flags (flags);
#else
	                for(j = 0; j < CHAR_HEIGHT - 2; j++, font_row++, dst += ipl) {
        	                rowbits = *font_row;
                	        data = fontm_bits[(rowbits>>4)&0xf];
	                        data = (data & fgmask) | (~data & bgmask);
        	                *dst = data;
                	        data = fontm_bits[rowbits&0xf];
	                        data = (data & fgmask) | (~data & bgmask);
        	                *(dst+1) = data;
                	}
	                rowbits = *font_row;
        	        data = fontm_bits[(rowbits>>4)&0xf];
                	data = (data & fgmask) | (~data & bgmask);
	                data2 = fontm_bits[rowbits&0xf];
        	        data2 = (data2 & fgmask) | (~data2 & bgmask);
                	rowbits = font_row[1];
	                data3 = fontm_bits[(rowbits>>4)&0xf];
        	        data3 = (data3 & fgmask) | (~data3 & bgmask);
                	data4 = fontm_bits[rowbits&0xf];
	                data4 = (data4 & fgmask) | (~data4 & bgmask);
        	        
                	/* Prevent cursor spots left on the screen */
			__save_and_cli(flags);
			
			if (idx != cursor_pos) {
	                	*dst = data;
        	        	*(dst+1) = data2;
                		dst += ipl;
	                	*dst = data3;
        	        	*(dst+1) = data4;
                	} else {
	                	under_cursor [0] = data;
        	        	under_cursor [1] = data2;
                		under_cursor [2] = data3;
	                	under_cursor [3] = data4;
        	        }
                	
	                __restore_flags(flags);
#endif
		}
		break;
	} /* case */
	} /* switch */
	return (0);
}

static void sbus_scr_writew(unsigned short val, unsigned short * addr)
{
        /*
         * always deposit the char/attr, then see if it was to "screen" mem.
	 * if so, then render the char/attr onto the real screen.
         */
	if (*addr != val) {
        	*addr = val;
		if ((unsigned long)addr < video_mem_term &&
		    (unsigned long)addr >= video_mem_base &&
		    vt_cons [fg_console]->vc_mode == KD_TEXT)
			sbus_blitc(val, (unsigned long) addr);
	}
}

static unsigned short sbus_scr_readw(unsigned short * addr)
{
	return *addr;
}

static void sbus_memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;

	count >>= 1;
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS) {
		while (count) {
			count--;
			*addr++ = c;
		}
		return;
	}
	if ((unsigned long) addr + count > video_mem_term ||
	    (unsigned long) addr < video_mem_base) {
	    	if ((unsigned long) addr + count <= video_mem_term ||
	    	    (unsigned long) addr > video_mem_base) {
			while (count) {
				count--;
				*addr++ = c;
			}
			return;
	    	} else {
			while (count) {
				count--;
				scr_writew(c, addr++);
			}
		}
#define GX_SETW (*fbinfo[0].setw)(x_margin + ((xoff - (addr - last)) << 3), y_margin + CHAR_HEIGHT * yoff, c, addr - last);
	} else if (do_accel) {
		int yoff = (((long)addr - (long)video_mem_base) >> 1) / video_num_columns;
		int xoff = (((long)addr - (long)video_mem_base) >> 1) % video_num_columns;
		unsigned short * last = addr;
		
		while (count) {
			count--;
			if (*addr != c) {
				if (xoff == video_num_columns) {
					if (last != addr)
						GX_SETW
					xoff = 0;
					yoff++;
					last = addr;
				}
				*addr++ = c;
				xoff++;
			} else {
				if (last != addr)
					GX_SETW
				if (xoff == video_num_columns) {
					xoff = 0;
					yoff++;
				}
				addr++;
				xoff++;
				last = addr;
			}
		}
		if (last != addr)
			GX_SETW
	} else {
		while (count) {
			count--;
			if (*addr != c) {
				sbus_blitc(c, (unsigned long)addr);
				*addr++ = c;
			} else
				addr++;
		}
	}
}

static void sbus_memcpyw(unsigned short *to, unsigned short *from, unsigned int count)
{
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS) {
		memcpy(to, from, count);
		return;
	}
	if ((unsigned long) to + count > video_mem_term ||
	    (unsigned long) to < video_mem_base) {
	    	if ((unsigned long) to + count <= video_mem_term ||
	    	    (unsigned long) to > video_mem_base)
	    	    	memcpy(to, from, count);
	    	else {
	    		count >>= 1;
			while (count) {
				count--;
				scr_writew(scr_readw(from++), to++);
			}
		}
#define GX_CPYW (*fbinfo[0].cpyw)(x_margin + ((xoff - (to - last)) << 3), y_margin + CHAR_HEIGHT * yoff, last, to - last);
	} else if (do_accel) {
		int yoff = (((long)to - (long)video_mem_base) >> 1) / video_num_columns;
		int xoff = (((long)to - (long)video_mem_base) >> 1) % video_num_columns;
		unsigned short * last = to;
		
		count >>=1;
		while (count) {
			count--;
			if (*to != *from) {
				if (xoff == video_num_columns) {
					if (last != to)
						GX_CPYW
					xoff = 0;
					yoff++;
					last = to;
				} else if (last != to && (*last & 0xff00) != (*from & 0xff00)) {
					GX_CPYW
					last = to;
				}
				*to++ = *from++;
				xoff++;
			} else {
				if (last != to)
					GX_CPYW
				if (xoff == video_num_columns) {
					xoff = 0;
					yoff++;
				}
				to++;
				xoff++;
				last = to;
				from++;
			}
		}
		if (last != to)
			GX_CPYW
	} else {
		count >>= 1;
		while (count) {
			count--;
			if (*to != *from) {
				sbus_blitc(*from, (unsigned long)to);
				*to++ = *from++;
			} else {
				from++;
				to++;
			}
		}
	}
}

#undef pos
int sbus_hw_scursor (struct fbcursor *cursor, fbinfo_t *fb)
{
	int op;
	int i, bytes = 0;
	struct fbcursor f;
	char red[2], green[2], blue[2];
	
	if (copy_from_user (&f, cursor, sizeof(struct fbcursor)))
		return -EFAULT;
	op = f.set;
	if (op & FB_CUR_SETSHAPE){
		if ((uint) f.size.fbx > fb->cursor.hwsize.fbx)
			return -EINVAL;
		if ((uint) f.size.fby > fb->cursor.hwsize.fby)
			return -EINVAL;
		if (f.size.fbx > 32)
			bytes = f.size.fby << 3;
		else
			bytes = f.size.fby << 2;
	}
	if (op & FB_CUR_SETCMAP){
		if (f.cmap.index || f.cmap.count != 2)
			return -EINVAL;
		if (copy_from_user (red, f.cmap.red, 2) ||
		    copy_from_user (green, f.cmap.green, 2) ||
		    copy_from_user (blue, f.cmap.blue, 2))
			return -EFAULT;
	}
	if (op & FB_CUR_SETCMAP)
		(*fb->setcursormap) (fb, red, green, blue);
	if (op & FB_CUR_SETSHAPE){
		uint u;
		
		fb->cursor.size = f.size;
		memset ((void *)&fb->cursor.bits, 0, sizeof (fb->cursor.bits));
		if (copy_from_user (fb->cursor.bits [0], f.mask, bytes) ||
		    copy_from_user (fb->cursor.bits [1], f.image, bytes))
			return -EFAULT;
		if (f.size.fbx <= 32) {
			u = ~(0xffffffff >> f.size.fbx);
			for (i = fb->cursor.size.fby - 1; i >= 0; i--) {
				fb->cursor.bits [0][i] &= u;
				fb->cursor.bits [1][i] &= fb->cursor.bits [0][i];
			}
		} else {
			u = ~(0xffffffff >> (f.size.fbx - 32));
			for (i = fb->cursor.size.fby - 1; i >= 0; i--) {
				fb->cursor.bits [0][2*i+1] &= u;
				fb->cursor.bits [1][2*i] &= fb->cursor.bits [0][2*i];
				fb->cursor.bits [1][2*i+1] &= fb->cursor.bits [0][2*i+1];
			}
		}
		(*fb->setcurshape) (fb);
	}
	if (op & (FB_CUR_SETCUR | FB_CUR_SETPOS | FB_CUR_SETHOT)){
		if (op & FB_CUR_SETCUR)
			fb->cursor.enable = f.enable;
		if (op & FB_CUR_SETPOS)
			fb->cursor.cpos = f.pos;
		if (op & FB_CUR_SETHOT)
			fb->cursor.chot = f.hot;
		(*fb->setcursor) (fb);
	}
	return 0;
}

static unsigned char hw_cursor_cmap[2] = { 0, 0xff };

void sbus_hw_hide_cursor (void)
{
	fbinfo[0].cursor.enable = 0;
	(*fbinfo[0].setcursor)(&fbinfo[0]);
	sbus_hw_cursor_shown = 0;
}

void sbus_hw_set_cursor (int xoff, int yoff)
{
	if (!sbus_hw_cursor_shown) {
		fbinfo[0].cursor.size.fbx = CHAR_WIDTH;
		fbinfo[0].cursor.size.fby = CHAR_HEIGHT;
		fbinfo[0].cursor.chot.fbx = 0;
		fbinfo[0].cursor.chot.fby = 0;
		fbinfo[0].cursor.enable = 1;
		memset (fbinfo[0].cursor.bits, 0, sizeof (fbinfo[0].cursor.bits));
		fbinfo[0].cursor.bits[0][CHAR_HEIGHT - 2] = 0xff000000;
		fbinfo[0].cursor.bits[1][CHAR_HEIGHT - 2] = 0xff000000;
		fbinfo[0].cursor.bits[0][CHAR_HEIGHT - 1] = 0xff000000;
		fbinfo[0].cursor.bits[1][CHAR_HEIGHT - 1] = 0xff000000;
		(*fbinfo[0].setcursormap) (&fbinfo[0], hw_cursor_cmap, hw_cursor_cmap, hw_cursor_cmap);
		(*fbinfo[0].setcurshape) (&fbinfo[0]);
		sbus_hw_cursor_shown = 1;
	}
	fbinfo[0].cursor.cpos.fbx = xoff;
	fbinfo[0].cursor.cpos.fby = yoff;
	(*fbinfo[0].setcursor)(&fbinfo[0]);
}

__initfunc(static void sbus_install_consops(void))
{
	suncons_ops.memsetw = sbus_memsetw;
	suncons_ops.memcpyw = sbus_memcpyw;
	suncons_ops.scr_writew = sbus_scr_writew;
	suncons_ops.scr_readw = sbus_scr_readw;

	suncons_ops.get_scrmem = sbus_get_scrmem;
	suncons_ops.set_scrmem = sbus_set_scrmem;

	suncons_ops.hide_cursor = sbus_hide_cursor;
	suncons_ops.set_cursor = sbus_set_cursor;
	suncons_ops.set_get_font = sbus_set_get_font;
	suncons_ops.con_adjust_height = sbus_con_adjust_height;
	suncons_ops.set_get_cmap = sbus_set_get_cmap;
	suncons_ops.set_palette = sbus_set_palette;
	suncons_ops.set_other_palette = sbus_set_other_palette;
	suncons_ops.console_restore_palette = sbus_console_restore_palette;

	suncons_ops.con_type_init = sbus_con_type_init;
	suncons_ops.con_type_init_finish = sbus_con_type_init_finish;

	suncons_ops.clear_screen = sbus_clear_screen;
	suncons_ops.render_screen = sbus_render_screen;
	suncons_ops.clear_margin = sbus_clear_margin;
}
