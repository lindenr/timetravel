#include "graphics.h"

#include <stdio.h>
#include <stdarg.h>

/* window dimensions (in pixels) */
int gr_ph = 0, gr_pw = 0, gr_pa = 0;

/* event callback functions */
void (*gr_onidle) () = NULL;
void (*gr_onresize) () = NULL;
void (*gr_onrefresh) () = NULL;
void (*gr_quit) () = NULL;

/* The following static variables are for internal use */

/* Screen data */
Uint32 *gr_pixels;
static int gr_pitch;

/* SDL globals */
static SDL_Window *sdlWindow;
static SDL_Renderer *sdlRenderer;
static SDL_Texture *sdlTexture;

/* timing parameters for held keys */
static uint32_t gr_kinitdelay = 10, gr_kdelay = 10;

/* state for keys being held down */
static uint32_t key_fire_ms = 0;
static char cur_key_down = 0;
static int num_keys_down = 0;

/* state for key timeout */
static uint32_t end = 0;

/* state for animation-skipping */
static char peeked = GRK_EOF;
static int gr_skip_anim = 0;

#ifdef DEBUG_GETCH_TIME
static uint32_t lastref = 0;
#endif

void gr_refresh ()
{
	if (gr_onrefresh)
		gr_onrefresh ();

	SDL_UpdateTexture (sdlTexture, NULL, gr_pixels, gr_pitch);
	SDL_RenderClear (sdlRenderer);
	SDL_RenderCopy (sdlRenderer, sdlTexture, NULL, NULL);
	SDL_RenderPresent (sdlRenderer);
}

int gr_inputcode (SDL_Keycode code)
{
	return (code >= 32 && code < 128);
}

int gr_inputch (char in)
{
	return (in >= 32 && in < 128);
}

static int gr_down_keys[256] = {0,};
static int gr_not_seen_up[256] = {0,};

int gr_is_pressed (char in)
{
	return gr_down_keys[(int)in];
}

int gr_is_pressed_debounce (char in)
{
	if (gr_down_keys[(int)in] && !gr_not_seen_up[(int)in])
	{
		gr_not_seen_up[(int)in] = 1;
		return 1;
	}
	return 0;
}

void gr_update_events ()
{
	SDL_Event sdlEvent;
	while (SDL_PollEvent (&sdlEvent))
	{
		char input_key = 0;
		SDL_Keycode code = 0;
		switch (sdlEvent.type)
		{
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if (sdlEvent.key.repeat)
					break;
				code = sdlEvent.key.keysym.sym;
				input_key = code%256;
				if (code == SDLK_UP)
					input_key = GRK_UP;
				else if (code == SDLK_DOWN)
					input_key = GRK_DN;
				else if (code == SDLK_LEFT)
					input_key = GRK_LT;
				else if (code == SDLK_RIGHT)
					input_key = GRK_RT;
				else if (code == SDLK_RETURN)
					input_key = GRK_RET;
				else if (code == SDLK_BACKSPACE)
					input_key = GRK_BS;
				else if (code == SDLK_ESCAPE)
					input_key = GRK_ESC;
				break;

			/*case SDL_VIDEORESIZE:
			{
				// TODO: something nice about window resizing
				gr_resize (event.resize.h, event.resize.w);
				break;
			}*/

			case SDL_WINDOWEVENT:
				if (sdlEvent.window.event == SDL_WINDOWEVENT_EXPOSED)
					gr_refresh ();
				break;
			
			case SDL_QUIT:
				if (!gr_quit)
					exit(0);
				/* this is allowed to return - can be interpreted as a hint
				 * to quit soon */
				gr_quit (); 
				break;
			
			default:
				break;
		}
		if (input_key)
		{
			gr_down_keys[(int)input_key] = !gr_down_keys[(int)input_key];
			if (gr_down_keys[(int)input_key])
				gr_not_seen_up[(int)input_key] = 0;
		}
	}
}

char gr_getch_aux (int text, int tout_num, int get)
{
	uint32_t ticks = gr_getms ();
#ifdef DEBUG_GETCH_TIME
	fprintf(stderr, "Time since last getch: %dms\n", ticks - lastref);
#endif
	gr_refresh ();

	if (peeked != GRK_EOF)
	{
		char ret = peeked;
		if (get)
		{
			peeked = GRK_EOF;
			gr_skip_anim = 0;
		}
		return ret;
	}

	if (tout_num > 0 && end <= ticks)
		end = tout_num + ticks;
	else if (tout_num <= 0)
		end = 0;
	SDL_Event sdlEvent;
	while (1)
	{
		ticks = gr_getms ();
		if (end && ticks >= end)
		{
			end = 0;
			break;
		}

		if (!SDL_PollEvent (&sdlEvent))
		{
			if (tout_num < 0)
				return GRK_EOF;
			if (cur_key_down && ticks >= key_fire_ms)
			{
				key_fire_ms = ticks + gr_kdelay;
				#ifdef DEBUG_GETCH_TIME
				lastref = ticks;
				#endif
				return cur_key_down;
			}
			if (gr_onidle)
				gr_onidle ();
			gr_wait (10, 0);
			continue;
		}

		char input_key = 0;
		SDL_Keycode code;
		switch (sdlEvent.type)
		{
			case SDL_TEXTINPUT:
				input_key = sdlEvent.text.text[0];
				break;
			case SDL_KEYDOWN:
				if (sdlEvent.key.repeat)
					break;
				code = sdlEvent.key.keysym.sym;
				if (gr_inputcode(code))
				{
					++ num_keys_down;
					if (num_keys_down == 1 && (!text) &&
					    (sdlEvent.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)))
					{
						input_key = GR_CTRL(code);
						break;
					}
				}
				if (code == SDLK_UP)
					input_key = GRK_UP;
				else if (code == SDLK_DOWN)
					input_key = GRK_DN;
				else if (code == SDLK_LEFT)
					input_key = GRK_LT;
				else if (code == SDLK_RIGHT)
					input_key = GRK_RT;
				else if (code == SDLK_RETURN)
					input_key = GRK_RET;
				else if (code == SDLK_BACKSPACE)
					input_key = GRK_BS;
				else if (code == SDLK_ESCAPE)
					input_key = GRK_ESC;
				break;

			case SDL_KEYUP:
				code = sdlEvent.key.keysym.sym;
				if (gr_inputcode(code))
					-- num_keys_down;
				if (!num_keys_down)
					cur_key_down = 0;
				break;

			/*case SDL_VIDEORESIZE:
			{
				// TODO: something nice about window resizing
				gr_resize (event.resize.h, event.resize.w);
				break;
			}*/

			case SDL_WINDOWEVENT:
				if (sdlEvent.window.event == SDL_WINDOWEVENT_EXPOSED)
					gr_refresh ();
				break;
			
			case SDL_QUIT:
				if (!gr_quit)
					exit(0);
				/* this is allowed to return - can be interpreted as a hint
				 * to quit soon */
				gr_quit (); 
				break;
			
			default:
				break;
		}
		if (input_key && input_key == cur_key_down)
			continue;
		else if (input_key)
		{
			cur_key_down = input_key;
			ticks = gr_getms ();
			key_fire_ms = ticks + gr_kinitdelay;
			#ifdef DEBUG_GETCH_TIME
			lastref = ticks;
			#endif
			if (!get)
				peeked = input_key;
			return input_key;
		}
	}
	return GRK_EOF;
}

char gr_getch ()
{
	return gr_getch_aux (0, 0, 1);
}

char gr_getch_text ()
{
	return gr_getch_aux (1, 0, 1);
}

char gr_getch_int (int t)
{
	return gr_getch_aux (0, t, 1);
}

void grx_getstr (int zloc, int yloc, int xloc, char *out, int len)
{
	int i = 0;
	while (1)
	{
		char in = gr_getch_text ();
		if (in == GRK_RET) break;
		else if (in == GRK_BS)
		{
			if (i)
			{
				-- xloc;
				-- i;
			}
			out[i] = 0;
			continue;
		}
		/* watershed - put non-input-char handling above here */
		else if (!gr_inputch (in)) continue;
		else if (i < len-1)
		{
			out[i] = in;
			++ xloc;
			++ i;
		}
	}
	out[i] = 0;
}

void gr_resize (int ph, int pw)
{
	gr_ph = ph;
	gr_pw = pw;
	gr_pa = ph*pw;
	
	gr_pitch = sizeof (Uint32) * gr_pw;
	gr_pixels = realloc (gr_pixels, gr_pitch * gr_ph);
	memset (gr_pixels, 0, gr_pitch * gr_ph);

	if (gr_onresize)
		gr_onresize ();
}

void gr_cleanup ()
{
	SDL_DestroyRenderer (sdlRenderer);
	SDL_DestroyWindow (sdlWindow);
	SDL_Quit ();
}

void gr_init (int ph, int pw)
{
	if (SDL_Init (SDL_INIT_VIDEO) < 0)
	{
		fprintf (stderr, "Error initialising SDL: %s\n", SDL_GetError ());
		exit (1);
	}

	atexit (gr_cleanup);
	sdlWindow = SDL_CreateWindow ("Yore", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		pw, ph, 0);
	if (sdlWindow == NULL)
	{
		fprintf (stderr, "SDL error: window is NULL\n");
		exit (1);
	}

	sdlRenderer = SDL_CreateRenderer (sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (sdlRenderer == NULL)
	{
		fprintf (stderr, "SDL error: renderer is NULL\n");
		exit (1);
	}
	SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
	SDL_RenderClear (sdlRenderer);
	SDL_SetRenderDrawBlendMode (sdlRenderer, SDL_BLENDMODE_NONE);
	sdlTexture = SDL_CreateTexture (sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pw, ph);

	gr_resize (ph, pw);
}

char gr_wait (uint32_t ms, int interrupt)
{
	if (!ms)
		return GRK_EOF;
	if (!interrupt)
	{
		SDL_Delay (ms);
		return GRK_EOF;
	}
	if (gr_skip_anim)
	{
		/* don't want to hang */
		SDL_Delay (1);
		return GRK_EOF;
	}
	char out = gr_getch_aux (0, ms, 0);
	if (out != GRK_EOF)
		gr_skip_anim = 1;
	return out;
}

uint32_t gr_getms ()
{
	return SDL_GetTicks ();
}

