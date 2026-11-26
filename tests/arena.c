#include "vtest.h"
#include "arena.h"
#include "macros.h"

/* ARENA_ALLOC deliberately overrides its default designated initializers. */
#pragma GCC diagnostic ignored "-Woverride-init"

#define LARGE_PARAMS(...)	.flags = ARENA_FLAG_LARGE_PAGES, __VA_ARGS__

/* Fills [p, p + size) with a byte pattern and checks it back. */
static _Bool
fill_and_check(u8* p, u32 size, u8 seed)
{
	for (u32 i = 0; i < size; i += 4096)
		p[i] = seed + (u8)(i >> 12);
	p[size - 1] = seed;

	for (u32 i = 0; i < size; i += 4096)
		if (p[i] != (u8)(seed + (u8)(i >> 12)))
			return 0;

	return p[size - 1] == seed;
}

TEST(ArenaPushPop)
{
	arena_t* arena = ARENA_ALLOC();

	ASSERT_SUCCESS(arena != 0);
	u32* a = ARENA_PUSH_ARRAY(arena, u32, 16);
	ASSERT_SUCCESS(a != 0);
	ASSERT_EQ(arena_pos(arena) >= ARENA_HEADER_SIZE + 16 * sizeof(u32));
	arena_clear(arena);
	ASSERT_EQ(arena_pos(arena) == ARENA_HEADER_SIZE);
	arena_release(arena);
}

TEST(ArenaLargePagesBaseAligned)
{
	arena_t* arena = ARENA_ALLOC(LARGE_PARAMS());

	ASSERT_SUCCESS(arena != 0);
	ASSERT_EQ(((u64)arena & (PAGE_LARGE_SIZE - 1)) == 0);
	ASSERT_EQ((arena->commit % PAGE_LARGE_SIZE) == 0);
	ASSERT_EQ((arena->reserve % PAGE_LARGE_SIZE) == 0);
	ASSERT_EQ(arena->commit >= PAGE_LARGE_SIZE);
	arena_release(arena);
}

TEST(ArenaLargePagesSizesRoundedUp)
{
	arena_t* arena = ARENA_ALLOC(LARGE_PARAMS(.reserve_size = MB(5), .commit_size = KB(100)));

	ASSERT_SUCCESS(arena != 0);
	ASSERT_EQ(arena->reserve == MB(6));
	ASSERT_EQ(arena->commit == MB(2));
	arena_release(arena);
}

TEST(ArenaLargePagesGrowCommit)
{
	arena_t* arena = ARENA_ALLOC(LARGE_PARAMS(.reserve_size = MB(16), .commit_size = MB(2)));

	ASSERT_SUCCESS(arena != 0);
	ASSERT_EQ(arena->commit == MB(2));

	/* Crosses the first large page: the header makes it end in the third one. */
	u8* p = arena_push(arena, MB(3), 16, 1);
	ASSERT_SUCCESS(p != 0);
	ASSERT_EQ(arena->current == arena);
	ASSERT_EQ(arena->commit == MB(4));
	ASSERT_EQ((arena->commit % PAGE_LARGE_SIZE) == 0);
	ASSERT_SUCCESS(fill_and_check(p, MB(3), 7));

	u8* q = arena_push(arena, MB(2), 16, 1);
	ASSERT_SUCCESS(q != 0);
	ASSERT_EQ(q >= p + MB(3));
	ASSERT_EQ(arena->commit == MB(6));
	ASSERT_SUCCESS(fill_and_check(q, MB(2), 11));
	ASSERT_SUCCESS(fill_and_check(p, MB(3), 7));

	arena_release(arena);
}

TEST(ArenaLargePagesChain)
{
	arena_t* arena = ARENA_ALLOC(LARGE_PARAMS(.reserve_size = MB(4), .commit_size = MB(2)));

	ASSERT_SUCCESS(arena != 0);

	u8* a = arena_push(arena, MB(3), 16, 1);
	u8* b = arena_push(arena, MB(3), 16, 1);

	ASSERT_SUCCESS(a != 0);
	ASSERT_SUCCESS(b != 0);
	ASSERT_EQ(arena->current != arena);
	ASSERT_EQ(arena->current->prev == arena);
	ASSERT_EQ(((u64)arena->current & (PAGE_LARGE_SIZE - 1)) == 0);
	ASSERT_EQ((arena->current->reserve % PAGE_LARGE_SIZE) == 0);
	ASSERT_SUCCESS(fill_and_check(a, MB(3), 1));
	ASSERT_SUCCESS(fill_and_check(b, MB(3), 2));

	/* A single push bigger than a block gets a dedicated large block. */
	u8* c = arena_push(arena, MB(9) + 1, 16, 1);
	ASSERT_SUCCESS(c != 0);
	ASSERT_EQ(((u64)arena->current & (PAGE_LARGE_SIZE - 1)) == 0);
	ASSERT_EQ((arena->current->reserve % PAGE_LARGE_SIZE) == 0);
	ASSERT_SUCCESS(fill_and_check(c, MB(9) + 1, 3));

	ASSERT_SUCCESS(fill_and_check(a, MB(3), 1));
	ASSERT_SUCCESS(fill_and_check(b, MB(3), 2));
	arena_release(arena);
}

TEST(ArenaLargePagesNoChain)
{
	arena_t* arena = ARENA_ALLOC(LARGE_PARAMS(.flags = ARENA_FLAG_LARGE_PAGES | ARENA_FLAG_NO_CHAIN,
						  .reserve_size = MB(4), .commit_size = MB(2)));

	ASSERT_SUCCESS(arena != 0);
	ASSERT_SUCCESS(arena_push(arena, MB(3), 16, 0) != 0);
	ASSERT_FAILURE(arena_push(arena, MB(3), 16, 0) != 0);
	ASSERT_EQ(arena->current == arena);
	arena_release(arena);
}

TEST(ArenaLargePagesPop)
{
	arena_t* arena = ARENA_ALLOC(LARGE_PARAMS(.reserve_size = MB(4), .commit_size = MB(2)));

	ASSERT_SUCCESS(arena != 0);

	u8* a = arena_push(arena, MB(1), 16, 1);
	arena_temp_t tmp = arena_temp_begin(arena);
	u8* b = arena_push(arena, MB(3), 16, 1);
	u8* c = arena_push(arena, MB(3), 16, 1);

	ASSERT_SUCCESS(a != 0 && b != 0 && c != 0);
	ASSERT_EQ(arena->current != arena);
	arena_temp_end(tmp);
	ASSERT_EQ(arena->current == arena);
	ASSERT_SUCCESS(fill_and_check(a, MB(1), 5));

	/* The arena is reusable after popping the chained blocks. */
	u8* d = arena_push(arena, MB(2), 16, 1);
	ASSERT_SUCCESS(d != 0);
	ASSERT_SUCCESS(fill_and_check(d, MB(2), 9));

	arena_clear(arena);
	ASSERT_EQ(arena_pos(arena) == ARENA_HEADER_SIZE);
	arena_release(arena);
}

TEST(ArenaLargePagesRepeat)
{
	/* Alloc/release cycles exercise the aligned-reservation trimming and munmap lengths. */
	for (u32 i = 0; i < 16; i++)
	{
		arena_t* arena = ARENA_ALLOC(LARGE_PARAMS(.reserve_size = MB(6), .commit_size = MB(2)));

		ASSERT_SUCCESS(arena != 0);
		ASSERT_EQ(((u64)arena & (PAGE_LARGE_SIZE - 1)) == 0);

		u8* p = arena_push(arena, MB(5), 8, 1);
		ASSERT_SUCCESS(p != 0);
		ASSERT_SUCCESS(fill_and_check(p, MB(5), (u8)i));
		arena_release(arena);
	}
}

TEST(ArenaDefaultUnchanged)
{
	arena_t* arena = ARENA_ALLOC();

	ASSERT_SUCCESS(arena != 0);
	ASSERT_EQ(arena->commit == ARENA_COMMIT_SIZE_DEFAULT);
	ASSERT_EQ(arena->reserve == ARENA_RESERVE_SIZE_DEFAULT);
	arena_release(arena);
}
