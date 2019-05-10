#include "graphics.h"
#include "vector.h"
#include <math.h>

const int blockwidth = 120, maxvel = 18;

#define MAX_SIMULTANEOUS_KEYS 10

// records which keys held down (up to a max number) and for how many frames
struct Keys
{
	char held[MAX_SIMULTANEOUS_KEYS]; // an initial segment is the keys held; padded with 0
	int frames;
};

struct PlayerRecording
{
	int i_plx, i_ply, i_plxv, i_plyv; // pos+velocity at start of recording
	int start; // start frame (always 0?)
	Vector inputs; // vector of struct Keys
	char prevheld[MAX_SIMULTANEOUS_KEYS], held[MAX_SIMULTANEOUS_KEYS]; // current and previous frame keys
	int prevnum, num; // number of keys held in current and prev frame
	int differ; // does cur frame contain a key not held in prev frame
	int curinput, curframe; // curinput = -1 if using player input; else records where we are in playback
};

struct PlayerState
{
	int plx, ply, plxv, plyv; // pos+velocity
	int on_ground; // can jump
	int plw; // width (and height) of player
	int extant; // currently in play
	struct PlayerRecording rec; // where to record keystrokes to/read from
};

struct LevelState
{
	int frame; // which frame (unused)
	char *level, *initlevel; // current and inital state of level
	char *ctrl; // what levers control what squares
	int levelw, levelh; // level dimensions
	int camx, camy; // camera location (pixels)
	Vector player_states; // all players' states
};

struct LevelState *ls_init (const char *level, const char *ctrl, int levelw)
{
	struct LevelState *ls = malloc (sizeof(struct LevelState));
	int len = strlen(level);
	*ls = (struct LevelState) {0, malloc(len+1), malloc(len+1), malloc(len+1), levelw, (len-1)/levelw + 1,
		0, 0, v_dinit (sizeof(struct PlayerState))};
	strcpy (ls->level, level);
	strcpy (ls->initlevel, level);
	strcpy (ls->ctrl, ctrl);
	return ls;
}

void ls_free (struct LevelState *ls)
{
	free (ls->level);
	free (ls->initlevel);
	free (ls->ctrl);
	int i;
	for (i = 0; i < ls->player_states->len; ++ i)
	{
		struct PlayerState *ps = v_at (ls->player_states, i);
		v_free (ps->rec.inputs);
	}
	v_free (ls->player_states);
	free (ls);
}

// whether controlled by player or recording is transparent to caller
int rec_isdown_aux (struct PlayerRecording *rec, char c, int (*pressed)(char))
{
	if (rec->curinput >= 0) // controlled by recording
	{
		int i;
		char *held = ((struct Keys *)v_at(rec->inputs, rec->curinput))->held;
		// read from recording:
		for (i = 0; i < MAX_SIMULTANEOUS_KEYS && held[i]; ++ i)
			if (held[i] == c)
				return 1; // c recorded as held
		return 0; // c not held
	}
	// not controlled by recording!
	// read keys from actual player using supplied function:
	if (!pressed (c))
		return 0;
	// c is being pressed!
	// add to recording if not there:
	int i;
	for (i = 0; i < MAX_SIMULTANEOUS_KEYS && rec->held[i]; ++ i)
	{
		if (rec->held[i] == c) // already have c recorded as pressed for current frame
			return 1;
	}
	if (i < MAX_SIMULTANEOUS_KEYS)
		rec->held[i] = c; // record c as pressed
	else
		return 0; // pretend not held (too many keys being held)
	rec->num ++;

	// check if definitely different to prev frame:
	if (rec->differ) // already know different from prev frame
		return 1;
	// check if c not pressed in prev frame
	int j;
	for (j = 0; j < MAX_SIMULTANEOUS_KEYS; ++ j)
		if (rec->prevheld[j] == c)
			break;
	if (j >= MAX_SIMULTANEOUS_KEYS) // not pressed in prev frame, so different
		rec->differ = 1;
	return 1;
}

int rec_isdown (struct PlayerRecording *rec, char c)
{
	return rec_isdown_aux (rec, c, gr_is_pressed);
}

int rec_isdown_debounce (struct PlayerRecording *rec, char c)
{
	return rec_isdown_aux (rec, c, gr_is_pressed_debounce);
}

// return values
// 0: continue as normal; 1: recording finished; 2: time-travels; 3: finish level
int rec_finishframe (struct PlayerRecording *rec)
{
	if (rec->curinput >= 0) // controlled by recording
	{
		rec->curframe ++; // next frame in same struct Keys
		if (rec->curframe >= ((struct Keys *)v_at(rec->inputs, rec->curinput))->frames)
		{
			// next struct Keys:
			rec->curframe = 0;
			rec->curinput ++;
			if (rec->curinput >= rec->inputs->len) // no more struct Keys
				return 1; // recording finished; player time-travelled back at this point
		}
		return 0;
	}
	int ret = 0;
	if (gr_is_pressed ('t'))
		ret = 2;
	else if (gr_is_pressed (GRK_RET))
		ret = 3;
	// if there is a prev frame, and it has the same # of held keys as the current one,
	// and no different ones, then they are the same set of keys (not necessarily same order)
	if (rec->inputs->len && rec->num == rec->prevnum && !rec->differ)
		// add one frame of the same:
		((struct Keys *)v_at (rec->inputs, rec->inputs->len - 1))->frames ++;
	else
	{
		// new key-frame hahahaha
		struct Keys k = {{0,}, 1};
		memcpy (k.held, rec->held, MAX_SIMULTANEOUS_KEYS);
		v_push (rec->inputs, &k);
		// copy cur to prev
		memcpy (rec->prevheld, rec->held, MAX_SIMULTANEOUS_KEYS);
		rec->prevnum = rec->num;
		rec->differ = 0;
	}
	// reset for next frame
	memset (rec->held, 0, MAX_SIMULTANEOUS_KEYS);
	rec->num = 0;
	return ret;
}

// jiggle players and their velocities to stop overlaps between player and level
void check_collisions (struct PlayerState *ps, struct LevelState *ls)
{
	int levelw = ls->levelw;
	char *level = ls->level;
	int plw = ps->plw, levelh = ls->levelh;
	if (ps->plx < 0)
	{
		ps->plx = 0;
		ps->plxv = 0;
	}
	else if (ps->plx + plw > levelw*blockwidth)
	{
		ps->plx = levelw*blockwidth - plw;
		ps->plxv = 0;
	}
	if (ps->ply < 0)
	{
		ps->ply = 0;
		ps->plyv = 0;
	}
	else if (ps->ply + plw > levelh*blockwidth)
	{
		ps->ply = levelh*blockwidth - plw;
		ps->plyv = 0;
		ps->on_ground = 1;
	}

	int xover = ps->plx%blockwidth + plw > blockwidth;
	int yover = ps->ply%blockwidth + plw > blockwidth;
	int b = ps->plx/blockwidth + levelw*(ps->ply/blockwidth); // within level[]
#define L(b) (level[b] == 'g')
	int A = L(b), B = (xover && L(b+1)),
		C = (yover && L(b+levelw)), D = (xover && yover && L(b+levelw+1));
#undef L

	int shouldprojx = 0, shouldprojy = 0;
	int xproj = ((ps->plxv < 0) ? plw : 0) - (ps->plx + plw)%blockwidth;
	int yproj = ((ps->plyv < 0) ? plw : 0) - (ps->ply + plw)%blockwidth;
	if (A && B && C && D)
		return;
	else if (A+B+C+D == 0)
		return;
	else if (A+B+C+D == 3)
		shouldprojx = shouldprojy = 1;
	else if ((A && B) || (C && D))
		shouldprojy = 1;
	else if ((A && C) || (B && D))
		shouldprojx = 1;
	else if (xover ^ yover)
	{
		shouldprojx = xover;
		shouldprojy = yover;
	}
	else if (ps->plxv >= 0 && (A || C))
		shouldprojy = 1;
	else if (ps->plxv <= 0 && (B || D))
		shouldprojy = 1;
	else if (ps->plyv >= 0 && (A || B))
		shouldprojx = 1;
	else if (ps->plyv <= 0 && (C || D))
		shouldprojx = 1;
	else
	{
		if (abs(xproj*ps->plyv) > abs(yproj*ps->plxv))
			shouldprojy = 1;
		else
			shouldprojx = 1;
	}

	if (shouldprojx)
	{
		ps->plx += xproj;
		ps->plxv = 0;
	}
	if (shouldprojy)
	{
		ps->ply += yproj;
		ps->plyv = 0;
		if (yproj < 0)
			ps->on_ground = 1;
	}
}

// toggle a lever with given id
void ls_toggle_ctrl (struct LevelState *ls, char id)
{
	int i;
	char *level = ls->level;
	char *ctrl = ls->ctrl;
	// find all blocks in level under control of our lever
	for (i = 0; ctrl[i]; ++ i)
	{
		if (ctrl[i] != id) // only care about things under control
			continue;
		switch (level[i])
		{
			// swap air with ground, and on-lever with off-lever
			case 'a': level[i] = 'g'; break;
			case 'g': level[i] = 'a'; break;
			case 'l': level[i] = 'L'; break;
			case 'L': level[i] = 'l'; break;
		}
	}
}

// toggle a lever at a location
void ls_lever (struct LevelState *ls, int b)
{
	char *level = ls->level;
	char *ctrl = ls->ctrl;
	if (level[b] != 'l' && level[b] != 'L')
		return;
	ls_toggle_ctrl (ls, ctrl[b]);
}

/* moves player/toggles levers etc according to input, then adjusts for collisions and finishes input
 * return values:
 * -1: dead or paradox, complete restart;
 * 0: normal, continue;
 * 1: finished replay;
 * 2: travelled back or left level
 * 3: finished level */
int next_player_state (struct LevelState *ls, struct PlayerState *ps)
{
	ps->plxv = 0;
	int b = ps->plx/blockwidth + ls->levelw*(ps->ply/blockwidth); // location
	if (ls->level[b] == 's') // on spikes; die
		return -1;
	// deal with input:
	struct PlayerRecording *rec = &(ps->rec);
	if (rec_isdown(rec, 'd'))
		ps->plxv += 5;
	if (rec_isdown(rec, 'a'))
		ps->plxv -= 5;
	if (rec_isdown(rec, 'w') && ps->on_ground)
		ps->plyv = -19;
	if (rec_isdown_debounce(rec, '.'))
		ls_lever (ls, b);
	// adjust:
	ps->plyv += 1; // gravity (positive y is downwards)
	if (ps->plxv < -maxvel) ps->plxv = -maxvel;
	if (ps->plxv >  maxvel) ps->plxv =  maxvel;
	if (ps->plyv < -maxvel) ps->plyv = -maxvel;
	if (ps->plyv >  maxvel) ps->plyv =  maxvel;
	ps->plx += ps->plxv;
	ps->ply += ps->plyv;
	ps->on_ground = 0;
	// collisions and input:
	check_collisions (ps, ls);
	int state = rec_finishframe (rec);
	if (state == 3 && ls->level[b] != '*')
		state = 0;
	return state;
}

#define PIXEL_VALUE(a,b,c) (((a)<<16) | ((b)<<8) | ((c)<<0) | 0xFF000000)
void draw_level (struct LevelState *ls)
{
	int w, x, y;
	char *level = ls->level;
	int levelw = ls->levelw;
	for (w = 0; w < gr_pa; ++ w)
		gr_pixels[w] = PIXEL_VALUE(255,255,w<gr_pa/2?255-2*(w/gr_pw*255)/gr_ph:0);
	for (w = 0; level[w]; ++ w)
	{
		int L = (w%levelw)*blockwidth, R = L + blockwidth,
			U = (w/levelw)*blockwidth, D = U + blockwidth;
		if (L >= ls->camx + gr_pw || R < ls->camx ||
			U >= ls->camy + gr_ph || D < ls->camy)
			continue;
		uint32_t val = 0;
		if (level[w] == 'g')
			val = PIXEL_VALUE(150, 100, 20);
		else if (level[w] == 'l')
			val = PIXEL_VALUE(100, 150, 100);
		else if (level[w] == 'L')
			val = PIXEL_VALUE(0, 200, 150);
		else if (level[w] == 's')
			val = PIXEL_VALUE(255, 0, 0);
		else if (level[w] == '*')
			val = PIXEL_VALUE(255, 200, 0);
		else
			continue;
		for (y = 0; y < blockwidth; ++ y) for (x = 0; x < blockwidth; ++ x)
		{
			int X = L + x - ls->camx, Y = U + y - ls->camy;
			if (X < 0 || X >= gr_pw ||
				Y < 0 || Y >= gr_ph)
				continue;
			gr_pixels[Y*gr_pw + X] = val;
		}
	}
	for (w = 0; w < ls->player_states->len; ++ w)
	{
		struct PlayerState *ps = v_at (ls->player_states, w);
		if (!ps->extant)
			continue;
		for (y = 0; y < 50; ++ y) for (x = 0; x < 50; ++ x)
		{
			int X = ps->plx + x - ls->camx, Y = ps->ply + y - ls->camy;
			if (X < 0 || X >= gr_pw ||
				Y < 0 || Y >= gr_ph)
				continue;
			gr_pixels[Y*gr_pw + X] = PIXEL_VALUE(0,ps->rec.curinput==-1?100:0,0);
		}
	}
	gr_refresh ();
	gr_wait (1, 0);
	gr_update_events ();
}

void new_player (struct LevelState *ls, const struct PlayerState *ps, int frame)
{
	struct PlayerState ps1 = {ps->plx, ps->ply, ps->plxv, ps->plyv, 0, 50, 1,
		{ps->plx, ps->ply, ps->plxv, ps->plyv, frame,
			v_dinit (sizeof(struct Keys)), {0,}, {0,}, 0, 0, 0, -1, 0}
	};
	v_push (ls->player_states, &ps1);
}

/* return values:
 * -1: dead, restart level;
 * 0: quit entirely;
 * 1: playerless playback, and no players extant: success!
 * 2: player time travelled back;
 * 3: player finished level */
int run_through_from_start (struct LevelState *ls)
{
	while (1) // loop through all frames
	{
		if (gr_is_pressed_debounce ('1'))
			ls_toggle_ctrl (ls, '1');
		if (gr_is_pressed_debounce ('2'))
			ls_toggle_ctrl (ls, '2');
		if (gr_is_pressed(GRK_ESC))
			return 0; // quit
		if (gr_is_pressed('r'))
			return -1; // reset

		// most recent player is currently player, camera follows them:
		struct PlayerState *ps = v_at (ls->player_states, ls->player_states->len-1);
		ls->camx = ps->plx + ps->plw/2 - gr_pw/2;
		if (ls->camx < 0)
			ls->camx = 0;
		else if (ls->camx > ls->levelw*blockwidth - gr_pw)
			ls->camx = ls->levelw*blockwidth - gr_pw;
		ls->camy = ps->ply + ps->plw/2 - gr_ph/2;
		if (ls->camy < 0)
			ls->camy = 0;
		else if (ls->camy > ls->levelh*blockwidth - gr_ph)
			ls->camy = ls->levelh*blockwidth - gr_ph;
		
		draw_level (ls);

		// loop through players and respond to input (recorded or live)
		int i, num_ext = 0;
		for (i = 0; i < ls->player_states->len; ++ i)
		{
			struct PlayerState *ps = v_at (ls->player_states, i);
			if (!ps->extant)
				continue;
			++ num_ext;
			int state = next_player_state (ls, ps);
			if (state == 1 && i < ls->player_states->len - 1)
			{
				// check matching pos+vel for end of cur player and start of next
				struct PlayerState *nps = v_at (ls->player_states, i+1);
				if (ps->plx != nps->rec.i_plx || ps->ply != nps->rec.i_ply ||
					ps->plxv != nps->rec.i_plxv || ps->plyv != nps->rec.i_plyv)
					return -1; // paradox!
			}
			if (state > 0)
				ps->extant = 0;
			if (state == -1 || state == 2 || state == 3)
				return state;
		}
		if (!num_ext) // no players left
			return 1;
	}
}

// reset player state to be played back as recording
void ps_reset (struct PlayerState *ps)
{
	ps->rec.curinput = 0;
	ps->rec.curframe = 0;
	ps->plx = ps->rec.i_plx;
	ps->ply = ps->rec.i_ply;
	ps->plxv = ps->rec.i_plxv;
	ps->plyv = ps->rec.i_plyv;
	ps->on_ground = 0;
	ps->extant = 1;
}

const char *initlevel, *control;
int levelw, i_plx, i_ply;

void setup1 ()
{
	initlevel =
	"aaaaaaaaaaa"
	"aaaaaaaaaaa"
	"aaaalaaaal*"
	"ggaagggaagg"
	"ggssgggssgg"
	"ggggggggggg";
	control =
	"00000000000"
	"00000000000"
	"00001000020"
	"00110002200"
	"00000000000"
	"00000000000";
	levelw = 11;
	i_plx = 100;
	i_ply = 100;
}

void setup2 ()
{
	initlevel =
	"aaaaaaaaaaaaa"
	"aaaaaaaaaaaaa"
	"aaaaagaaaaaaa"
	"aaaalgaaaaal*"
	"gaaaggaagaagg"
	"ggaaaaaggssgg"
	"ggggggggggggg";
	control =
	"0000000000000"
	"0000000000000"
	"0000000000000"
	"0000100000020"
	"0002000001100"
	"0000000000000"
	"0000000000000";
	levelw = 13;
	i_plx = 50;
	i_ply = 220;
}

void setup3 ()
{
	initlevel =
	"aaaaaaaaaaa"
	"aaaaaaaaaaa"
	"aaaalaaaaa*
	"ggaaggggggg"
	"ggssgggssgg"
	"ggggggggggg";
	control =
	"00000000000"
	"00000000000"
	"00001000000"
	"00110001100"
	"00000000000"
	"00000000000";
	levelw = 11;
	i_plx = 100;
	i_ply = 100;
}

int playlevel ()
{
	struct LevelState *ls = ls_init (initlevel, control, levelw); // set up level
	struct PlayerState ips = {i_plx, i_ply, 0, 0, }; // initial player pos+vel

	int state = 0;
	while (state != 3) // while not finished level
	{
		new_player (ls, &ips, 0); // make new player with given starting params
		state = run_through_from_start (ls); // play thru with all players
		if (state <= 0) // -1 restart level, or 0 quit game
		{
			ls_free (ls);
			return state;
		}
		
		// next inital player state is current (live) player's final state:
		struct PlayerState *ps = v_at (ls->player_states, ls->player_states->len - 1);
		ips = (struct PlayerState) {ps->plx, ps->ply, ps->plxv, ps->plyv, };

		// reset level and player states before adding new player
		strcpy (ls->level, ls->initlevel);
		int i;
		for (i = 0; i < ls->player_states->len; ++ i)
			ps_reset (v_at (ls->player_states, i));
	}
	// state == 3, level finished; everything reset
	// final fully-recorded runthrough to check consistency:
	state = run_through_from_start (ls); // -1 restart (paradox); 0 quit; 1 success
	ls_free (ls); // clean up
	return state;
}

int repeatlevel ()
{
	int status;
	while (1)
	{
		status = playlevel ();
		if (status >= 0)
			return status;
	}
}

int main ()
{
	gr_init (720, 1300);
	setup1 ();
	if (!repeatlevel ())
		return 0;
	setup2 ();
	if (!repeatlevel ())
		return 0;
	setup3 ();
	if (!repeatlevel ())
		return 0;
}

