#include "tetmino.h"

unsigned short tetmino_shapes[7][4] = {
	{TETMINO_I, TETMINO_I2, TETMINO_I,  TETMINO_I2 << 1},
	{TETMINO_S, TETMINO_S2, TETMINO_S,  TETMINO_S2 << 1},
	{TETMINO_T, TETMINO_T2, TETMINO_T3, TETMINO_T4},
	{TETMINO_O, TETMINO_O,  TETMINO_O,  TETMINO_O},
	{TETMINO_L, TETMINO_L2, TETMINO_L3, TETMINO_L4},
	{TETMINO_Z, TETMINO_Z2, TETMINO_Z,  TETMINO_Z2 << 1},
	{TETMINO_J, TETMINO_J2, TETMINO_J3, TETMINO_J4}
};

/* get mask to test collision */
static unsigned get_blocks_box(const blocks_row *blocks, int col)
{
	unsigned box = 0, row_mask = (1 << PIECE_WIDTH) - 1;
	int i;
	for (i=0; i < PIECE_HEIGHT; i++)
		box = (box << PIECE_WIDTH) | ((blocks[i] >> col) & row_mask);

	return box;
}

/* add or remove blocks */
static void xor_blocks_box(blocks_row *blocks, int col, unsigned shape)
{
	unsigned row_mask = (1 << PIECE_WIDTH) - 1;
	int i;
	for (i = PIECE_HEIGHT; i > 0; i--) {
		blocks[i-1] ^= (shape & row_mask) << col;
		shape >>= PIECE_WIDTH;
	}
}

void xor_tetmino(const struct tetmino *t, blocks_row *blocks)
{
	xor_blocks_box(blocks + t->row, t->col, t->shape);
}

int drop_height(const struct tetmino *t, const blocks_row *blocks, int max)
{
	int h = 0;

	if (max > t->row)
		max = t->row;

	if (max > 0) {
		unsigned box = get_blocks_box(blocks += t->row - 1, t->col);

		while (!(t->shape & box)) {
			h++;
			if (h == max)
				break;
			box = (box >> PIECE_WIDTH) |
			      (blocks[-h] >> t->col
			       << (PIECE_HEIGHT-1) * PIECE_WIDTH);
		}
	}
	return h;
}

/* doesn't handle the I piece! */
static int floor_kick(struct tetmino *t, unsigned rotated, unsigned box)
{
	if ((rotated & box) >> (PIECE_HEIGHT-1) * PIECE_WIDTH &&
	    !(rotated & (box << PIECE_WIDTH)) && t->floor_reached >= t->row) {
		t->floor_reached = t->row;
		t->row++;
		return 1;
	}
	return 0;
}

static int wall_kick(struct tetmino *t, unsigned rotated, unsigned box,
		     const blocks_row *blocks)
{
	unsigned overlap = rotated & box,
		 mask1 = TETMINO_I2 >> 1,	/* left column */
		 mask3 = TETMINO_I2 << 1;	/* right column */
	int	n = 0;

	/* kick if exactly one of the columns overlaps */
	if (overlap == (overlap & mask1))
		n = 1;
	if (overlap == (overlap & mask3))
		n = -1;

	if (n && !(rotated & get_blocks_box(blocks, t->col + n))) {
		t->col += n;
		return n;
	}
	return 0;
}

static int rotate(struct tetmino *t, const blocks_row *blocks, int n)
{
	const unsigned short *rot_states = tetmino_shapes[t->piece];
	unsigned box = get_blocks_box(blocks, t->col);
	int kick = 1;
	int i;

	/* try both vertical orientations of I, S and Z */
	if (rot_states[0] == t->shape &&
	    rot_states[2] == t->shape && (rot_states[n] & box)) {
		n += 2;
		kick = 0;
	}

	for (i=0; i<4; i++) {
		if (rot_states[i] == t->shape) {
			unsigned rotated = rot_states[(i+n) & 3];
			if (rotated & box) {
				if (!kick)
					return 0;
				/* J3, L3, T3 */
				if (i == 2 && floor_kick(t, rotated, box))
					;
				else if (!wall_kick(t, rotated, box, blocks))
					return 0;
			}
			t->shape = rotated;
			return rotated ^ TETMINO_O;
		}
	}
	return 0;
}

static int drop(struct tetmino *t, const blocks_row *blocks, int n)
{
	int h = drop_height(t, blocks, n);
	t->row -= h;
	return h;
}

int control_tetmino(struct tetmino *t, const blocks_row *blocks, enum action a)
{
	int moved = 0, dropped = 0, lock = 0;

	switch (a) {
	case ROTATE_CW:
		moved = rotate(t, blocks + t->row, 1);
		break;
	case ROTATE_CCW:
		moved = rotate(t, blocks + t->row, 3);
		break;
	case MOVE_RIGHT:
		t->col += (moved = !(t->shape & get_blocks_box(blocks + t->row,
							       t->col + 1)));
		break;
	case MOVE_LEFT:
		t->col -= (moved = !(t->shape & get_blocks_box(blocks + t->row,
							       t->col - 1)));
		break;
	case HARDDROP:
		dropped = drop(t, blocks, t->row);
		lock = 1;
		break;
	case SOFTDROP:
		dropped = drop(t, blocks, 1);
		lock = !dropped;
	default:
		break;
	}
	if (lock) {
		t->falling = 0;
		t->lock_delay_move = 0;
		t->lock_delay_step = 0;
	}

	/* any successful shifting or rotation */
	if (moved) {
		t->lock_delay_move = LOCK_DELAY_MOVE + 1;

		/* floor kicked - prevent "infinite spin" */
		if (t->floor_reached < t->row)
			drop(t, blocks, 1);
	}
	return moved | dropped;
}

int update_tetmino(struct tetmino *t, const blocks_row *blocks, int gravity)
{
	int dropped;

	if (!t->falling) {
		/* prevent floating piece */
		t->falling = drop_height(t, blocks, 1);

		if (!t->falling) {
			if (t->lock_delay_move > 0)
				t->lock_delay_move--;
			if (t->lock_delay_step > 0)
				t->lock_delay_step--;
			return 0;
		}
	}

	if (t->falling > gravity)
		t->falling = gravity;

	/* count down frames until drop */
	if (t->falling > 0) {
		t->falling--;
		if (t->falling)
			return 0;
	}
	/* if negative, drop many rows */
	dropped = drop(t, blocks, 1 - t->falling);

	/* test hit ground */
	if (dropped < 1 - t->falling) {
		t->falling = 0;
		t->lock_delay_move = LOCK_DELAY_MOVE;
		t->lock_delay_step = LOCK_DELAY_STEP;
	} else {
		t->falling = gravity;
	}

	if (dropped)
		t->floor_reached = t->row;

	return dropped;
}
