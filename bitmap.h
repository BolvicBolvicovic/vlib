#ifndef BITMAP_H
#define BITMAP_H

#include "c_types.h"
#include <assert.h>

#define BITMAP_CHUNK_FULL	0xffffffff
#define BITMAP_CHUNK_SIZE	32

typedef struct bitmap_t bitmap_t;

/* Name: bitmap_t
 * Description: bitmap struct with BITMAP_CHUNK_SIZE of 32 bits.
 * */
struct bitmap_t
{
	u32*	map;
	u32	size;
	u32	position;
};

/* Name: bitmap_set_bit
 * Description: sets a bit in the bitmap.
 * */
void bitmap_set_bit(bitmap_t* map, u32 bit);

/* Name: bitmap_unset_bit
 * Description: unsets a bit in the bitmap.
 * */
void bitmap_unset_bit(bitmap_t* map, u32 bit);

/* Name: bitmap_test_bit
 * Description: returns the value of a bit in the bitmap (1 or 0).
 * */
u32  bitmap_test_bit(bitmap_t* map, u32 bit);

/* Name: bitmap_find_next_free
 * Description: finds the next free bit in the bitmap.
 * On error, returns -1.
 * */
s32  bitmap_find_next_free_bit(bitmap_t* bitmap);
s32  bitmap_find_next_free_chunk(bitmap_t* bitmap);

#ifdef BITMAP_IMPL

void
bitmap_set_bit(bitmap_t* map, u32 bit)
{
	assert(bit < map->size);
	map->map[bit / BITMAP_CHUNK_SIZE] |= (1u << (bit % BITMAP_CHUNK_SIZE));
}

void
bitmap_unset_bit(bitmap_t* map, u32 bit)
{
	assert(bit < map->size);
	map->map[bit / BITMAP_CHUNK_SIZE] &= ~(1u << (bit % BITMAP_CHUNK_SIZE));
}

u32
bitmap_test_bit(bitmap_t* map, u32 bit)
{
	assert(bit < map->size);
	return map->map[bit / BITMAP_CHUNK_SIZE] & (1u << (bit % BITMAP_CHUNK_SIZE));
} 

s32
bitmap_find_next_free_bit(bitmap_t* bitmap)
{
	u32	word_count = bitmap->size/BITMAP_CHUNK_SIZE; 
	u32	i = bitmap->position;

	for (u32 visited = 0; visited < word_count; visited++, i = (i + 1) % word_count)
	{
		if (bitmap->map[i] == BITMAP_CHUNK_FULL) continue;

		for (u32 j = 0; j < BITMAP_CHUNK_SIZE; j++)
		{
			if ((bitmap->map[i] & 1u << j)) continue;

			if (j + 1 < BITMAP_CHUNK_SIZE)
				bitmap->position = i;
			else
				bitmap->position =  (i + 1) % word_count;

			return i * BITMAP_CHUNK_SIZE + j;
		}
	}

	return -1;
}

s32
bitmap_find_next_free_chunk(bitmap_t* bitmap)
{
	u32	word_count = bitmap->size/BITMAP_CHUNK_SIZE; 
	u32	i = bitmap->position;

	for (u32 visited = 0; visited < word_count; visited++, i = (i + 1) % word_count)
	{
		if (bitmap->map[i]) continue;

		bitmap->position =  (i + 1) % word_count;

		return i * BITMAP_CHUNK_SIZE;
	}

	return -1;
}

#endif // BITMAP_IMPL

#endif // BITMAP_H
