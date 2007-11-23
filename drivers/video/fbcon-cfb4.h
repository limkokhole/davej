    /*
     *  4 bpp packed pixel (cfb4)
     */

extern struct display_switch fbcon_cfb4;
extern void fbcon_cfb4_setup(struct display *p);
extern void fbcon_cfb4_bmove(struct display *p, int sy, int sx, int dy, int dx,
			     int height, int width);
extern void fbcon_cfb4_clear(struct vc_data *conp, struct display *p, int sy,
			     int sx, int height, int width);
extern void fbcon_cfb4_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx);
extern void fbcon_cfb4_putcs(struct vc_data *conp, struct display *p,
			     const char *s, int count, int yy, int xx);
extern void fbcon_cfb4_revc(struct display *p, int xx, int yy);