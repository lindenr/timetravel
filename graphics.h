#ifndef GRAPHICS_H_INCLUDED
#define GRAPHICS_H_INCLUDED

#include "SDL.h"
#include <stdint.h>

#ifdef main
#  undef main
#endif

/* Prefixes:
 * gr_ is the graphics prefix for generic things to do with the screen;
 * grx_ is the graph prefix for messing with a Graph fully;
 * gra_ for just in 2d */

extern int gr_ph, gr_pw, gr_pa;
extern Uint32 *gr_pixels;

//extern void (*gr_onidle) ();
extern void (*gr_onresize) ();
//extern void (*gr_onrefresh) ();
extern void (*gr_quit) ();

/* Initialisation */
void gr_init      (int ph, int pw);

/* Output */
void gr_refresh   ();

/* Input */
int gr_is_pressed (char in);
int gr_is_pressed_debounce (char in);
void gr_update_events ();

char gr_getch     ();
char gr_getch_text();
char gr_getch_int (int);
void grx_getstr   (int z, int y, int x, char *, int);
#define gra_getstr(g,y,x,s,i) (grx_getstr ((g), 0, (y), (x), (s), (i)))

char gr_wait      (uint32_t, int);
uint32_t gr_getms ();
void gr_resize    (int, int);

/* control-key of a lower-case character */
#define GR_CTRL(ch) ((ch)-96)

/* end-of-input reached */
#define GRK_EOF      ((char)0xFF)
#undef EOF // causes too many promotion problems

/* arrow keys */
#define GRK_UP       0x1E
#define GRK_DN       0x1F
#define GRK_LT       0xAE
#define GRK_RT       0xAF

/* Unusual input characters */
#define GRK_BS       0x08
#define GRK_TAB      0x09
#define GRK_RET      0x0D
#define GRK_ESC      0x1B

#endif /* GRAPHICS_H_INCLUDED */

