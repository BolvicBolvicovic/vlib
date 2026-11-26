#ifndef ARENA_H
#define ARENA_H

#include "c_types.h"
#include "bits.h"
#include "sizes.h"

#define PAGE_SIZE	KB(4)
/* Huge page size used by ARENA_FLAG_LARGE_PAGES. It is a constant (not queried at runtime)
 * because the arena layout needs it at compile time: 2 MiB is the huge page size of
 * x86-64 and of aarch64 with 4K base pages. */
#define PAGE_LARGE_SIZE	MB(2)

typedef u32	arena_flags_t;
enum
{
	ARENA_FLAG_NO_CHAIN	= BIT0,
	ARENA_FLAG_LARGE_PAGES	= BIT1,
};

#define ARENA_FLAGS_DEFAULT		0
#define ARENA_COMMIT_SIZE_DEFAULT	KB(64)
#define ARENA_RESERVE_SIZE_DEFAULT	MB(64)
#define ARENA_HEADER_SIZE		128

/* Name: arena_parameters_t
 * Description: parameters to create a arena_t that are passed to arena_alloc.
 * */
typedef struct arena_parameters_t arena_parameters_t;
struct arena_parameters_t
{
	arena_flags_t	flags;
	u32		reserve_size;
	u32		commit_size;
	void*		optional_backbuffer;
	char*		allocation_site_file;
	s32		allocation_site_line;
};

/* Name: arena_t
 * Description: structure that contains the information related to a kernel arena (stack in the heap). 
 * */
typedef struct arena_t arena_t;
struct arena_t
{
	arena_t*	prev;
	arena_t*	current;
	arena_flags_t	flags;
	u32		position;
	u32		base_position;
	u32		reserve_size;
	u32		commit_size;
	u32		reserve;
	u32		commit;
	char*		allocation_site_file;
	s32		allocation_site_line;
};

/* Name: arena_temp_t
 * Description: structure for temporary scope in kernel arenas.
 * */
typedef struct arena_temp_t arena_temp_t;
struct arena_temp_t
{
	arena_t*	arena;
	u32		pos;
};

/* arena_t creation/destruction */
arena_t*	arena_alloc(arena_parameters_t*);
#define ARENA_ALLOC(...) arena_alloc(&(arena_parameters_t){		\
		.flags 			= ARENA_FLAGS_DEFAULT,		\
		.reserve_size 		= ARENA_RESERVE_SIZE_DEFAULT,	\
		.commit_size 		= ARENA_COMMIT_SIZE_DEFAULT,	\
		.allocation_site_file 	= __FILE__,			\
		.allocation_site_line 	= __LINE__,			\
	       	__VA_ARGS__})
void		arena_release(arena_t*);

/* arena_t push/pop/pos core funtions */
void*		arena_push(arena_t*, u32 size, u32 align, u8 zero);
u32		arena_pos(arena_t*);
void		arena_pop_to(arena_t*, u32 pos);

/* arena_t push/pop helpers */
void		arena_clear(arena_t*);
void		arena_pop(arena_t*, u32 amount);

/* arena_temp_t scopes */
arena_temp_t	arena_temp_begin(arena_t*);
void		arena_temp_end(arena_temp_t);

/* arena_t push helper macros */
#define ARENA_PUSH_ARRAY_NO_ZERO_ALIGNED(a, T, c, align)\
	(T *)arena_push((a), sizeof(T)*(c), (align), 0)
#define ARENA_PUSH_ARRAY_ALIGNED(a, T, c, align)	\
	(T *)arena_push((a), sizeof(T)*(c), (align), 1)
#define ARENA_PUSH_ARRAY_NO_ZERO(a, T, c)		\
	ARENA_PUSH_ARRAY_NO_ZERO_ALIGNED(a, T, c, 4)
#define ARENA_PUSH_ARRAY(a, T, c)			\
	ARENA_PUSH_ARRAY_ALIGNED(a, T, c, 4)
#define ARENA_PUSH_STRUCT(a, T)				\
	ARENA_PUSH_ARRAY(a, T, 1)

/** Define this macro in one of the project files (main.c by default).
 *  A good pattern to be sure that there is a single implementation could be:
 *  #define ARENA_IMPL
 *  #include "arena.h"
 *  #undef ARENA_IMPL
 */
#ifdef ARENA_IMPL

#include "vlist.h"
#include "macros.h"
#include <string.h>

#ifdef _WIN32

/* NOGDI keeps windows.h from defining ERROR, which macros.h uses. */
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include <windows.h>

#ifndef MEM_LARGE_PAGES
#define MEM_LARGE_PAGES	0x20000000
#endif

/* Windows reserves with VirtualAlloc(MEM_RESERVE) and commits with VirtualAlloc(MEM_COMMIT).
 * A reservation is released whole with VirtualFree(MEM_RELEASE), the size is not used. */
#define ARENA_COMMIT(ptr, size)	(VirtualAlloc((ptr), (size), MEM_COMMIT, PAGE_READWRITE) != 0)
#define ARENA_UNMAP(ptr, size)	VirtualFree((ptr), 0, MEM_RELEASE)

/* Large pages need SeLockMemoryPrivilege, which is held but disabled by default.
 * Returns 1 if the privilege is now enabled. */
static int
arena_enable_lock_memory_privilege(void)
{
	HANDLE			token;
	TOKEN_PRIVILEGES	tp;
	int			ok = 0;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
		return 0;

	tp.PrivilegeCount		= 1;
	tp.Privileges[0].Attributes	= SE_PRIVILEGE_ENABLED;

	if (LookupPrivilegeValueA(0, "SeLockMemoryPrivilege", &tp.Privileges[0].Luid) &&
	    AdjustTokenPrivileges(token, FALSE, &tp, 0, 0, 0) &&
	    GetLastError() == ERROR_SUCCESS)
		ok = 1;

	CloseHandle(token);
	return ok;
}

#else

#include <sys/mman.h>

#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT	26
#endif
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB	(21 << MAP_HUGE_SHIFT)
#endif

#define ARENA_COMMIT(ptr, size)	(mprotect((ptr), (size), PROT_READ | PROT_WRITE) == 0)
#define ARENA_UNMAP(ptr, size)	munmap((ptr), (size))

#endif

#if defined(__GNUC__) || defined(__CLANG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif

arena_t*
arena_alloc(arena_parameters_t* params)
{
	u32	reserve_size	= params->reserve_size;
	u32	commit_size	= params->commit_size;
	u32	page_size	= PAGE_SIZE;
	u32	large_size	= PAGE_LARGE_SIZE;

#ifdef _WIN32
	SYSTEM_INFO	info;
	SIZE_T		large_min = GetLargePageMinimum();

	GetSystemInfo(&info);
	page_size = info.dwPageSize;
	/* Keep PAGE_LARGE_SIZE as the floor so layout stays a multiple of it. */
	if (large_min > large_size)
		large_size = (u32)large_min;
#endif

	if (params->flags & ARENA_FLAG_LARGE_PAGES)
	{
		reserve_size	= ALIGN(reserve_size, large_size);
		commit_size	= ALIGN(commit_size, large_size);
	}
	else
	{
		reserve_size	= ALIGN(reserve_size, page_size);
		commit_size	= ALIGN(commit_size, page_size);
	}

	void*	base = params->optional_backbuffer;

	if (base == 0)
	{
#ifdef _WIN32
		if (params->flags & ARENA_FLAG_LARGE_PAGES)
		{
			/* Real large pages are committed at reservation, so the whole block is
			 * committed and never grows. They need SeLockMemoryPrivilege, which is
			 * usually missing: then use normal pages. */
			base = VirtualAlloc(0, reserve_size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
					    PAGE_READWRITE);

			if (base == 0 &&
			    GetLastError() == ERROR_PRIVILEGE_NOT_HELD &&
			    arena_enable_lock_memory_privilege())
				base = VirtualAlloc(0, reserve_size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
						    PAGE_READWRITE);

			if (base != 0)
				commit_size = reserve_size;
			else
			{
				/* A reservation cannot be trimmed on Windows: find an aligned address,
				 * release, and reserve exactly there (retry if someone took it). */
				for (u32 attempt = 0; attempt < 8 && base == 0; attempt++)
				{
					u8*	raw = VirtualAlloc(0, (u64)reserve_size + large_size,
								   MEM_RESERVE, PAGE_NOACCESS);

					if (raw == 0)
						break;

					VirtualFree(raw, 0, MEM_RELEASE);
					base = VirtualAlloc((void*)ALIGN((u64)raw, (u64)large_size),
							    reserve_size, MEM_RESERVE, PAGE_NOACCESS);
				}

				if (base == 0)
					base = VirtualAlloc(0, reserve_size, MEM_RESERVE, PAGE_NOACCESS);

				if (base != 0 && !ARENA_COMMIT(base, commit_size))
				{
					ARENA_UNMAP(base, reserve_size);
					return 0;
				}
			}
		}
		else
		{
			base = VirtualAlloc(0, reserve_size, MEM_RESERVE, PAGE_NOACCESS);

			if (base != 0 && !ARENA_COMMIT(base, commit_size))
			{
				ARENA_UNMAP(base, reserve_size);
				return 0;
			}
		}
#else
		if (params->flags & ARENA_FLAG_LARGE_PAGES)
		{
			/* Explicit huge pages first. They are taken from the hugetlbfs pool,
			 * which is empty on most machines, so failure is expected. */
			base = mmap(0, reserve_size, PROT_NONE,
				    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);

			if (base == MAP_FAILED)
			{
				/* Fallback: normal pages, base aligned to the large page size, and
				 * a hint so transparent huge pages can back the range. */
				u8*	raw = mmap(0, (u64)reserve_size + large_size, PROT_NONE,
						   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

				if (raw == MAP_FAILED)
					return 0;

				u8*	aligned = (u8*)ALIGN((u64)raw, (u64)large_size);
				u8*	raw_end = raw + (u64)reserve_size + large_size;

				if (aligned > raw)
					munmap(raw, aligned - raw);
				munmap(aligned + reserve_size, raw_end - (aligned + reserve_size));

				base = aligned;
#ifdef MADV_HUGEPAGE
				madvise(base, reserve_size, MADV_HUGEPAGE);
#endif
			}

			if (mprotect(base, commit_size, PROT_READ | PROT_WRITE) != 0)
			{
				munmap(base, reserve_size);
				return 0;
			}
		}
		else
		{
			base = mmap(0, reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			mprotect(base, commit_size, PROT_READ | PROT_WRITE);
		}
#endif
	}

#ifdef _WIN32
	if (base == 0)
#else
	if (base == 0 || base == MAP_FAILED)
#endif
		return 0;

	arena_t*	arena		= (arena_t*)base;

	arena->current			= arena;
	arena->flags			= params->flags;
	arena->commit_size		= params->commit_size;
	arena->reserve_size		= params->reserve_size;

	/* Chained blocks and commit growth must stay multiples of the large page size. */
	if (params->flags & ARENA_FLAG_LARGE_PAGES)
	{
		arena->commit_size	= commit_size;
		arena->reserve_size	= reserve_size;
	}

	arena->base_position		= 0;
	arena->position			= ARENA_HEADER_SIZE;
	arena->commit			= commit_size;
	arena->reserve			= reserve_size;
	arena->allocation_site_file	= params->allocation_site_file;
	arena->allocation_site_line	= params->allocation_site_line;

	return arena;
}

void
arena_release(arena_t* arena)
{
	for (arena_t *n = arena->current, *prev = 0; n; n = prev)
	{
		prev = n->prev;
		ARENA_UNMAP(n, n->reserve);
	}
}

void*
arena_push(arena_t* arena, u32 size, u32 align, u8 zero)
{
	arena_t*	current = arena->current;
	u32		pos_pre = ALIGN(current->position, align);
	u32		pos_pst = pos_pre + size;

	if (current->reserve < pos_pst && !(arena->flags & ARENA_FLAG_NO_CHAIN))
	{
		u32	res_size = current->reserve_size;
		u32	cmt_size = current->commit_size;

		if (size + ARENA_HEADER_SIZE > res_size)
		{
			res_size = ALIGN(size + ARENA_HEADER_SIZE, align);
			cmt_size = ALIGN(size + ARENA_HEADER_SIZE, align);
		}

		arena_t*	new_block = ARENA_ALLOC(
				.reserve_size		= res_size,
				.commit_size		= cmt_size,
				.flags			= current->flags,
				.allocation_site_file	= current->allocation_site_file,
				.allocation_site_line	= current->allocation_site_line);

		if (new_block == 0)
			return 0;

		new_block->base_position = current->base_position + current->reserve;
		new_block->prev = arena->current;
		arena->current = new_block;

		current = new_block;
		pos_pre = ALIGN(current->position, align);
		pos_pst = pos_pre + size;
	}

	u32	size_to_zero = 0;

	if (zero)
		size_to_zero = MIN(current->commit, pos_pst) - pos_pre;

	if (current->commit < pos_pst)
	{
		u32	commit_post_aligned	= pos_pst + current->commit_size - 1;
		commit_post_aligned 		-=  commit_post_aligned % current->commit_size;
		u32	commit_post_clamped	= CLAMP_TOP(commit_post_aligned, current->reserve);
		u32	commit_size		= commit_post_clamped - current->commit;
		void*	commit_ptr		= (void*)((u64)current + current->commit);

		/* Large page arenas have commit_size/commit/reserve multiples of PAGE_LARGE_SIZE
		 * and an aligned base, so this commits whole huge pages. */
		(void)ARENA_COMMIT(commit_ptr, commit_size);

		current->commit = commit_post_clamped;
	}

	void*	result = 0;

	if (current->commit < pos_pst)
		return 0;

	result = (u8*)current + pos_pre;
	current->position = pos_pst;
	
	if (size_to_zero)
		memset(result, 0, size_to_zero);

	return result;
}

u32
arena_pos(arena_t* arena)
{
	arena_t*	current	= arena->current;
	u32		pos	= current->base_position + current->position;

	return pos;
}

void
arena_pop_to(arena_t* arena, u32 pos)
{
	u32		big_pos = CLAMP_BOT(ARENA_HEADER_SIZE, pos);
	arena_t*	current = arena->current;

	for (arena_t* prev = 0; current->base_position >= big_pos; current = prev)
	{
		prev = current->prev;
		ARENA_UNMAP(current, current->reserve);
	}

	u32	new_pos = big_pos - current->base_position;

	if (new_pos > current->position)
		return;
	
	arena->current		= current;
	current->position	= new_pos;
}

inline void
arena_clear(arena_t* arena)
{
	arena_pop_to(arena, 0);
}

void
arena_pop(arena_t* arena, u32 amount)
{
	u32	pos_old = arena_pos(arena);
	u32	pos_new = pos_old;

	if (amount < pos_old)
		pos_new = pos_old - amount;

	arena_pop_to(arena, pos_new);
}

inline arena_temp_t
arena_temp_begin(arena_t* arena)
{
	u32		pos = arena_pos(arena);
	arena_temp_t	temp= { arena, pos };

	return temp;
}

inline void
arena_temp_end(arena_temp_t temp)
{
	arena_pop_to(temp.arena, temp.pos);
}

#if defined(__GNUC__) || defined(__CLANG__)
#pragma GCC diagnostic pop
#endif

#endif // ARENA_IMPL

#endif // ARENA_H
