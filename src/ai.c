
#include <math.h>
#include "tommyhashtbl.h"

#define BIGN unsigned long long int
#define UINT unsigned int
#define HASHTABLE_SIZE 131072
#define TABLE_SIZE 65536
#define ROWMASK 0xffff
#define TILEMASK 0xf
#define TILES 16
#define DIRECTIONS 4
#define LOSE_PENALTY -200000.0
#define NOMOVE_SCORE -900000.0

unsigned int convert_tile(int val);
BIGN convert_to_bign(int grid[]);
BIGN combine_bign(UINT n1, UINT n2, UINT n3, UINT n4);
void init_mappers(void);
BIGN transpose(BIGN bn);
double get_score(BIGN bn);
unsigned char count_zeros(BIGN bn);
BIGN move_left(BIGN bn, BIGN tbn);
BIGN move_right(BIGN bn, BIGN tbn);
BIGN move_up(BIGN bn, BIGN tbn);
BIGN move_down(BIGN bn, BIGN tbn);
double do_system_move(BIGN bn, unsigned char depth, unsigned char start);
double do_player_move(BIGN bn, unsigned char depth, unsigned char start);
int get_best_move(BIGN bn, unsigned char depth);
int get_worst_tile(BIGN bn, unsigned char depth);
void clear_cache(void);
int next_player_move(UINT n1, UINT n2, UINT n3, UINT n4);
int next_system_tile(UINT n1, UINT n2, UINT n3, UINT n4);

typedef BIGN(*movement_fn)(BIGN, BIGN);
static movement_fn MOVERS[DIRECTIONS] = { move_up, move_right, move_down, move_left };

// used to cache parts of the expectimax search
typedef struct {
	BIGN bn;
	unsigned char depth;
	double score;
	tommy_node node;
} SearchCache;

static tommy_hashtable hashtable;

// maintain a reference to all SearchCache objects for easy clearing of hashtable
static SearchCache *caches[HASHTABLE_SIZE];
static int next_cache = 0;

// pre-calculated tables
static UINT TB_MOVE_LEFT[TABLE_SIZE] = { 0 };
static UINT TB_MOVE_RIGHT[TABLE_SIZE] = { 0 };
static BIGN TB_MOVE_UP[TABLE_SIZE] = { 0 };
static BIGN TB_MOVE_DOWN[TABLE_SIZE] = { 0 };
static BIGN TB_TRANSPOSE[TABLE_SIZE] = { 0 };
static unsigned char TB_ZEROS[TABLE_SIZE] = { 0 };
static double TB_SCORE[TABLE_SIZE] = { 0 };

UINT convert_tile(int val) {
	if (val <= 1) return 0U;
	UINT ret = 0U;
	while (val >>= 1) ++ret;
	return ret;
}

// convert a game matrix to a 64 bit int
// where each tile takes up 4 bits of space
BIGN convert_to_bign(int grid[]) {
	BIGN bn = 0ULL;
	for (int i = 0; i < TILES; i++) {
		bn <<= 4ULL;
		bn |= (BIGN) convert_tile(grid[i]);
	}
	return bn;
}

// combine 4 ints representing each row into 1 64 bit int
BIGN combine_bign(UINT n1, UINT n2, UINT n3, UINT n4) {
	BIGN bn = ((BIGN) n1) << 48ULL;
	bn |= ((BIGN) n2) << 32ULL;
	bn |= ((BIGN) n3) << 16ULL;
	return bn | n4;
}

// populate the movement and scoring tables
void init_mappers(void) {
	// go through all possible row states
	for (UINT r = 0U; r < TABLE_SIZE; r++) {
		UINT row[] = { r & TILEMASK, (r >> 4) & TILEMASK, (r >> 8) & TILEMASK, (r >> 12) & TILEMASK };
		TB_TRANSPOSE[r] = ((BIGN) row[3] << 48ULL) | ((BIGN) row[2] << 32ULL) |\
			((BIGN) row[1] << 16ULL) | (BIGN) row[0];

		UINT lj = 3;
		UINT rj = 0;
		UINT li = 3;
		UINT lpv = 0;
		UINT rpv = 0;
		unsigned char zeros = 0;
		UINT highest = 0;
		double ssum1 = 0.0;

		for (int ri = 0; ri < 4; ri++) {
			// R-L search (for move right & down)
			UINT e = row[ri];
			if (e != 0) {
				if (rpv == 0) {
					rpv = e;
				} else if (rpv == e) {
					TB_MOVE_RIGHT[r] |= (e + 1U) << (rj * 4U);
					TB_MOVE_DOWN[r] |= ((BIGN) e + 1ULL) << (rj * 16ULL);
					rpv = 0;
					rj++;
				} else {
					TB_MOVE_RIGHT[r] |= rpv << (rj * 4U);
					TB_MOVE_DOWN[r] |= ((BIGN) rpv) << (rj * 16ULL);
					rpv = e;
					rj++;
				}
			}
			// L-R search (for move left & up)
			e = row[li];
			if (e == 0)
				zeros++;
			else {
				ssum1 += pow((double) e, 1.5);
				if (e > highest)
					highest = e;

				if (lpv == 0) {
					lpv = e;
				} else if (lpv == e) {
					TB_MOVE_LEFT[r] |= (e + 1U) << (lj * 4U);
					TB_MOVE_UP[r] |= ((BIGN) e + 1) << (lj * 16ULL);
					lpv = 0;
					lj--;
				} else {
					TB_MOVE_LEFT[r] |= lpv << (lj * 4U);
					TB_MOVE_UP[r] |= ((BIGN) lpv) << (lj * 16ULL);
					lpv = e;
					lj--;
				}
			}
			li--;
		}

		TB_MOVE_LEFT[r] |= lpv << (lj * 4U);
		TB_MOVE_UP[r] |= ((BIGN) lpv) << (lj * 16ULL);
		TB_MOVE_RIGHT[r] |= rpv << (rj * 4U);
		TB_MOVE_DOWN[r] |= ((BIGN) rpv) << (rj * 16ULL);
		TB_ZEROS[r] = zeros;

		// scoring heuristics

		int sinc = 0; // number of tiles in increasing order
		int sdec = 0; // number of tiles in decreasing order
		double sclose = 0.0; // sub-score measuring closeness of tiles
		for (int i = 0; i < 3; i++) {
			if (row[i] >= row[i + 1]) sinc++;
			if (row[i] <= row[i + 1]) sdec++;

			int diff = (int) row[i] - (int) row[i + 1];
			if (row[i] < row[i + 1]) diff = (int) row[i + 1] - (int) row[i];
			if (row[i] == row[i + 1]) // bonus if same value
				sclose += pow(row[i] + 1.0, 1.6) * 2.2;
			else if (diff <= 1) { // small bonus if differ by 1
				if (row[i] > row[i + 1]) sclose += pow((double) row[i], 1.5);
				else sclose += pow((double) row[i + 1], 1.5);
			} else // penalize if differ by 2 or more
				sclose -= pow((double) diff, 1.6) * 1.2;
		}

		// + zeros: number of zeros; encourage merging
		// + sclose: closeness of tiles; promote boards that allow for easy merging
		// - ssum1: sum of e**1.5; encourage merging
		double score = 2304.0 + (double) zeros * 341.0 + sclose * 22.0 - ssum1 * 45.0;

		// bonus if highest tile is at the side; penalize if otherwise
		// aim: promote boards with a more orderly arrangement
		if (row[0] == highest || row[3] == highest)
			score += pow((double) highest, 1.5575) * 39.0;
		else
			score -= pow((double) highest, 1.5575) * 39.0;

		// bonus if tiles are fully increasing/decreasing; penalize if otherwise
		// aim: promote boards with a more orderly arrangement
		if (sinc == 3 || sdec == 3)
			score += 982.0;
		else
			score -= 982.0;
		
		TB_SCORE[r] = score;
	}
}

BIGN transpose(BIGN bn) {
	BIGN tbn = TB_TRANSPOSE[bn & ROWMASK];
	tbn |= TB_TRANSPOSE[(bn >> 16) & ROWMASK] << 4;
	tbn |= TB_TRANSPOSE[(bn >> 32) & ROWMASK] << 8;
	return tbn | TB_TRANSPOSE[(bn >> 48) & ROWMASK] << 12;
}

// score every row and column using the scoring table
double get_score(BIGN bn) {
	double score = TB_SCORE[bn & ROWMASK];
	score += TB_SCORE[(bn >> 16) & ROWMASK];
	score += TB_SCORE[(bn >> 32) & ROWMASK];
	score += TB_SCORE[(bn >> 48) & ROWMASK];
	BIGN tbn = transpose(bn);
	score += TB_SCORE[tbn & ROWMASK];
	score += TB_SCORE[(tbn >> 16) & ROWMASK];
	score += TB_SCORE[(tbn >> 32) & ROWMASK];
	score += TB_SCORE[(tbn >> 48) & ROWMASK];
	return score;
}

unsigned char count_zeros(BIGN bn) {
	unsigned char n = TB_ZEROS[bn & ROWMASK];
	n += TB_ZEROS[(bn >> 16) & ROWMASK];
	n += TB_ZEROS[(bn >> 32) & ROWMASK];
	return n + TB_ZEROS[(bn >> 48) & ROWMASK];
}

BIGN move_left(BIGN bn, BIGN tbn) {
	BIGN nbn = TB_MOVE_LEFT[bn & ROWMASK];
	nbn |= ((BIGN) TB_MOVE_LEFT[(bn >> 16) & ROWMASK]) << 16;
	nbn |= ((BIGN) TB_MOVE_LEFT[(bn >> 32) & ROWMASK]) << 32;
	nbn |= ((BIGN) TB_MOVE_LEFT[(bn >> 48) & ROWMASK]) << 48;
	return nbn;
}

BIGN move_right(BIGN bn, BIGN tbn) {
	BIGN nbn = TB_MOVE_RIGHT[bn & ROWMASK];
	nbn |= ((BIGN) TB_MOVE_RIGHT[(bn >> 16) & ROWMASK]) << 16;
	nbn |= ((BIGN) TB_MOVE_RIGHT[(bn >> 32) & ROWMASK]) << 32;
	nbn |= ((BIGN) TB_MOVE_RIGHT[(bn >> 48) & ROWMASK]) << 48;
	return nbn;
}

BIGN move_up(BIGN bn, BIGN tbn) {
	BIGN nbn = TB_MOVE_UP[tbn & ROWMASK];
	nbn |= TB_MOVE_UP[(tbn >> 16) & ROWMASK] << 4;
	nbn |= TB_MOVE_UP[(tbn >> 32) & ROWMASK] << 8;
	nbn |= TB_MOVE_UP[(tbn >> 48) & ROWMASK] << 12;
	return nbn;
}

BIGN move_down(BIGN bn, BIGN tbn) {
	BIGN nbn = TB_MOVE_DOWN[tbn & ROWMASK];
	nbn |= TB_MOVE_DOWN[(tbn >> 16) & ROWMASK] << 4;
	nbn |= TB_MOVE_DOWN[(tbn >> 32) & ROWMASK] << 8;
	nbn |= TB_MOVE_DOWN[(tbn >> 48) & ROWMASK] << 12;
	return nbn;
}

// used for hashtable searches
int hashtable_compare(const void* arg, const void* obj) {
	return *(const BIGN*)arg != ((const SearchCache *)obj)->bn;
}

// search every possible new tile positions and take the average score
double do_system_move(BIGN bn, unsigned char depth, unsigned char start) {
	BIGN key = bn + (BIGN) depth * 9997;
	SearchCache *cache = tommy_hashtable_search(&hashtable, hashtable_compare, &bn, key);
	if (cache) {
		return cache->score;
	}

	unsigned char diff = start - depth;
	double score = 0;
	int count = 0;
	BIGN tile = bn;

	if (depth == 1) { // search is ending - calculate score of each resultant board
		for (int i = 0; i < TILES; i++) {
			if ((tile & TILEMASK) == 0) { // find empty tiles
				count++;
				if (diff > 4) {
					score += get_score(bn | (1ULL << (i * 4ULL)));
				} else {
					score += get_score(bn | (1ULL << (i * 4ULL))) * 0.9;
					score += get_score(bn | (2ULL << (i * 4ULL))) * 0.1;
				}
			}
			tile >>= 4ULL;
		}
	} else { // continue searching next level
		for (int i = 0; i < TILES; i++) {
			if ((tile & TILEMASK) == 0) { // find empty tiles
				count++;
				if (diff > 4) {
					score += do_player_move(bn | (1ULL << (i * 4ULL)), depth - 1, start);
				}
				else {
					score += do_player_move(bn | (1ULL << (i * 4ULL)), depth - 1, start) * 0.9;
					score += do_player_move(bn | (2ULL << (i * 4ULL)), depth - 1, start) * 0.1;
				}
			}
			tile >>= 4ULL;
		}
	}

	cache = malloc(sizeof(SearchCache));
	cache->bn = bn;
	cache->depth = depth;
	if (count > 0) { // take average score
		score = score / (double) count;
		cache->score = score;
		tommy_hashtable_insert(&hashtable, &cache->node, cache, key);
		caches[next_cache++] = cache;
		return score;
	}

	// no empty tile
	if (depth == 1) {
		score = get_score(bn);
		cache->score = score;
		tommy_hashtable_insert(&hashtable, &cache->node, cache, key);
		caches[next_cache++] = cache;
		return score;
	} else {
		score = do_player_move(bn, depth - 1, start);
		cache->score = score;
		tommy_hashtable_insert(&hashtable, &cache->node, cache, key);
		caches[next_cache++] = cache;
		return score;
	}
}

// search every direction and take the highest score
double do_player_move(BIGN bn, unsigned char depth, unsigned char start) {
	BIGN key = bn + (BIGN)depth * 9997;
	SearchCache *cache = tommy_hashtable_search(&hashtable, hashtable_compare, &bn, key);
	if (cache) {
		return cache->score;
	}

	BIGN tbn = transpose(bn);
	BIGN _bn;
	double _sc;

	double score = LOSE_PENALTY;
	if (depth == 1) { // search is ending - calculate score of each resultant board
		for (int i = 0; i < DIRECTIONS; i++) {
			_bn = MOVERS[i](bn, tbn);
			if (_bn == bn) continue;
			_sc = get_score(_bn);
			if (_sc > score)
				score = _sc;
		}
	} else { // continue searching next level
		for (int i = 0; i < DIRECTIONS; i++) {
			_bn = MOVERS[i](bn, tbn);
			if (_bn == bn) continue;
			_sc = do_system_move(_bn, depth - 1, start);
			if (_sc > score)
				score = _sc;
		}
	}

	cache = malloc(sizeof(SearchCache));
	cache->bn = bn;
	cache->depth = depth;
	cache->score = score;
	tommy_hashtable_insert(&hashtable, &cache->node, cache, key);
	caches[next_cache++] = cache;

	return score;
}

// get highest scoring direction
// 0: up, 1: right, 2: down, 3: left
int get_best_move(BIGN bn, unsigned char depth) {
	double score = NOMOVE_SCORE;
	int drt = 0;
	BIGN tbn = transpose(bn);
	for (int i = 0; i < DIRECTIONS; i++) {
		BIGN _bn = MOVERS[i](bn, tbn);
		if (_bn == bn) continue;
		double _sc = do_system_move(_bn, depth - 1, depth);
		if (_sc > score) {
			score = _sc;
			drt = i;
		}
	}
	return drt;
}

// get lowest scoring tile placement position
int get_worst_tile(BIGN bn, unsigned char depth) {
	double score = 9999999.0;
	double _sc;
	int id = 0;
	BIGN tile = bn;
	for (int i = 0; i < TILES; i++) {
		if ((tile & TILEMASK) == 0) {
			_sc = do_player_move(bn | (1ULL << (i * 4ULL)), depth - 1, depth) * 0.9;
			_sc += do_player_move(bn | (2ULL << (i * 4ULL)), depth - 1, depth) * 0.1;
			if (_sc < score) {
				score = _sc;
				id = i;
			}
		}
		tile >>= 4ULL;
	}
	return id;
}

// clear the hashtable and free all SearchCache objects
void clear_cache(void) {
	for (int i = 0; i < next_cache; i++) {
		tommy_hashtable_remove_existing(&hashtable, &caches[i]->node);
		free(caches[i]);
	}
	next_cache = 0;
}


// the functions below should be exported by emscripten


int next_player_move(UINT n1, UINT n2, UINT n3, UINT n4) {
	clear_cache();
	BIGN bn = combine_bign(n1, n2, n3, n4);
	unsigned char zeros = count_zeros(bn);
	unsigned char depth = 6;
	if (zeros < 8)
		depth = 7;
	if (zeros < 3)
		depth = 9;
	return get_best_move(bn, depth);
}

int next_system_tile(UINT n1, UINT n2, UINT n3, UINT n4) {
	clear_cache();
	BIGN bn = combine_bign(n1, n2, n3, n4);
	unsigned char zeros = count_zeros(bn);
	unsigned char depth = 6;
	if (zeros < 5)
		depth = 7;
	return get_worst_tile(bn, depth);
}

int main(void) {
	init_mappers();
	tommy_hashtable_init(&hashtable, HASHTABLE_SIZE);
}
