# VLIB

Simple headers only C lib/toolchain to build personnal projects.

## Quick Start

To start a project:

```bash
mkdir NewCProject
cd NewCProject
git clone git@github.com:BolvicBolvicovic/vlib.git
./vlib/gen_makefile.py NewCProjectExecutable
```

Include the header needed direcly in your files.
In case using files containing functions declarations, define the implementation in the main file of the project.

```c
// At the top of main.c
#define HEADER_NAME_IMPL
#include "vlib/header_name.h"
```

In the Makefile, add your `.c` files to the `SRCS` variable at the end of the addprefix directive and then:

```bash
make
./NewCProjectExecutable
```

The toolchain contains a lightweight test framework.
You can add your tests files to the `./tests` folder and in the Makefile, to the addprefix directive at the end of the `TSRCS` variable.

```bash
make tests
./NewCProjectExecutable-tests
```

More information on how to use it can be found in the header `vtest.h`.

The toolchain contains also a compile_commands.json generator (which is a `bear` wrapper) for clangd users:

```bash
make commands
```

## Table of Content

- [arena.h](#arena.h)
- [bitmap.h](#bitmap.h)
- [bits.h](#bits.h)
- [c_types.h](#c_types.h)
- [gen_makefile.py](#gen_makefile.py)
- [macros.h](#macros.h)
- [readfile.h](#readfile.h)
- [sizes.h](#sizes.h)
- [swissmap.h](#swissmap.h)
- [vlist.h](#vlist.h)
- [vtest.h](#vtest.h)
- [zstring.h](#zstring.h)

## arena.h

`arena_t` follows more or less the implementation done by Ryan Fleury for the [raddebugger](https://github.com/EpicGames/raddebugger/tree/master/src) in 2025 or so.
I did it before he ported the raddebugger to linux and there was only the windows implementation at the time (if I recall correctly).
I decided to start using them after reading the [Untangling Lifetimes](https://www.dgtlgrove.com/p/untangling-lifetimes-the-arena-allocator).
Essentially, it is a stack allocator that can extend itself and is used as a lifetime for a group of entities (in opposition to a lifetime per entity) making tracability of allocations on the heap much easier.
I have not found a use-case that forced me to use `malloc` since then and it reduced memory leaks drastically.

### Usage

Arenas work per thread (not for shared data in a multithreaded app) and hold a lifetime for multiple allocations.

```c
#ARENA_IMPL
#include "vlib/arena.h"
#include <stdio.h>

void
foo(arena_t* arena)
{
    arena_temp_t tmp = arena_temp_begin(arena); // Surround code with begin and end.
    u32         *arr = ARENA_PUSH_ARRAY(arena, u32, 10); // Bound to foo.

    bar(arena, arr); // Pointer cannot be leaked here and arena can be used for more allocations.

    arena_temp_end(tmp); // zero cost "free"
}

s32
main(void)
{
    // ARENA_ALLOC can be parameterized (see arena.h)
    arena_t* main_arena  = ARENA_ALLOC(); // mmap syscall
    arena_t* frame_arena = ARENA_ALLOC(); // mmap syscall

    // foo uses arena_temp_t, so at exit, arena will remain the same.
    // We could surround foo here with arena_temp_t instead of inside of it
    // to let the caller decide of the lifetime of the allocations made by the callee.
    foo(main_arena);

    while (1)
    {
        // Things pushed to main_arena are saved in the main context.
        // While things pushed to frame_arena will end with the current frame.
        foobar(main_arena, frame_arena);
        arena_clear(frame_arena);
    }
    
    // If one needs to cleanup at the end of a program or a thread:
    arena_release(main_arena);  // munmap syscall
    arena_release(frame_arena); // munmap syscall
    
    return 0;
}
```

### TODO Roadmap

- Handle big pages.
- Support different operating systems when needed.

## bitmap.h

32 bits bitmap coming from my kernel from scratch.

### Usage

```c
#define BITMAP_IMPL
#include "vlib/bitmap.h"

s32
main(void)
{
    u32      map       = 0;
    bitmap_t bitmap    = { .map=&map, .size=32 };
    u32      flags[32] = { 0 };

    for (u32 i = 0; i < bitmap.size; i++)
    {
        if (i % 2) continue;
        flags[i] = 1;
        bitmap_set_bit(&bitmap, i);
    }
    
    for (u32 i = 0; i < bitmap.size; i++)
    {
        assert(bitmap_test_bit(&bitmap, i) == flags[i]);
    }

    return 0;
}
```

### TODO Roadmap

- Add a `bitmap_new` function if needed.

## bits.h

Macros defining each 32 bits.

### Usage

```c
#include "vlib/bits.h"
#include "vlib/c_types.h"
#include <stdio.h>

typedef enum   Type   Type;
typedef struct Header Header;
typedef struct Plex   Plex;

enum Type
{
    TypeByte  = BIT0,
    TypeWord  = BIT1,
    TypeDword = BIT2,
    TypeError = BIT3
};

struct Header
{
    Type  type;
    char* name;
};

struct Plex
{
    Header header;
    union
    {
        u8    byte;
        u16   word;
        u32   dword;
        char* error_msg;
    }
};
```

### TODO Roadmap

- Maybe verbose helper but I'd rather write the bitwise operations myself at the moment.

## c_types.h

Redefinition of C types to my taste.

### Usage

```c
#include "vlib/c_types.h"

const u32          my_const   = 42; // This
const unsigned int your_const = 24; // Instead of this.
```

### TODO Roadmap

*None*

## gen_makefile.py

Vibe-coded makefile generator with hand-written modifications.
Generates a Makefile based on the `./src` folder. If it does not exists, creates it alongside a `src/main.c` file.
Also creates a `./tests` folder used by the `make tests` command which uses the vtest framework embedded in this toolchain.

### Usage

```bash
mkdir NewCProject
cd NewCProject
git clone git@github.com:BolvicBolvicovic/vlib.git
./vlib/gen_makefile.py NewCProjectExecutable
```

### TODO Roadmap

*None*

## macros.h

Macro bundle coming from my calc project which actually turned a simple calculator into a kind of weird programming language.

### TODO Roadmap

- Might either delete it or heavily refactor it.

## readfile.h

Single function that reads a file into a dynamic buffer allocated with an arena.

### TODO Roadmap

- Might extend to more IO functionalites in the future.

## sizes.h

Macros for memory size.

## swissmap.h

Fast hashmap with fixed capacity. Inspired by [google's implementation](https://www.youtube.com/watch?v=ncHmEUmJZf4).
It uses SSE2 to find tags in buckets with two instructions.

### Usage

Macros to declare and define the map and its functions work in a very similar manner as to template.

```c
#define SWISSMAP_IMPL
#define ARENA_IMPL
#include "vlib/arena.h"
#include "vlib/swissmap.h"


SWISSMAP_DECLARE_DEFAULT(ZStringMap, zstring*);
SWISSMAP_DEFINE_FUNCTIONS_DEFAULT(ZStringMap, zstring*);

int
main(void)
{
    arena_t*    arena = ARENA_ALLOC();
    ZStringMap* map   = ZStringMap_new(arena, 100);
    zstring*    zstr  = ARENA_PUSH_STRUCT(arena, zstring);

    zstring_ptr_from_literal(zstr, "zstr");
    ZStringMap_put("zstr", zstr);

    zstring**   pzstr = ZStringMap_get("zstr");

    if (!pzstr)
    {
        return 1;
    }

    zstring*   result = *pzstr;

    zstring_print(result);

    return 0;
}
```

### TODO Roadmap

- Make it extensible.

## vlist.h

Single linked list primitive with its functions helper push/pop/insert.

### TODO Roadmap

- Add support for doubly linked list.

## vtest.h

VTEST is test framework aimed for small size project.
It is easy to use for it has a minimal amount of features by design.

### Usage

A suite is a .c file containing some tests.
A good practice is to use directories if one needs to specify a kind of suite.

```c
// Example: in test/extensions/new_extensions.c
#include "vtest.h"
#include "new_extensions.h"

TEST(FeatureFoo)
{
    ASSERT_SUCCESS(foo("bar") == FeatureOk);
}

TEST_WITH_ARG(FeatureBar, _bar)
{
    Bar* bar = _bar;
    ASSERT_FAILURE(bar->func("foo") == 0);
}

SETUP_SUITE()
{
    TEST_INPUT(FeatureBar, Bar, .func=bAR);
    TEST_INPUT(FeatureBar, Bar, .func=baR);
}
```

Compile and link all tests files with vtest.c
and run the executable.
`--help` shows the available options.

### TODO Roadmap

- Add a logger which prints based on an enum.
- Consider tagging tests that want to execute the pre/post meta-functions.
- Improve ASSERT feedback.
- Filter option which would run all suites containing the user input. Same for tests.
- Runtime controle options (i.e. `--repeat`, etc...).
- Standard report of elapsed time. 
- Wrap Vtest in the runner with a meta-Vtest which contains runtime information.
- JSON output for CI.

## zstring.h

Zero-Is-Initialization string which is fast, secure and interoperable.
Once defined, a zstring should remain constant.

### Usage

--- Initialization ---
Note that for an empty zstring, one should do the following:
`zstring	zstr = zstring_on_stack(0, 0);`
or
`zstring*	zstr = &zstring_on_stack(0, 0);`
or
`zstring	zstr = { 0 };`
or
`zstring*	zstr = &(zstring){ 0 };`
or
`zstring*	zstr = ARENA_PUSH_STRUCT(arena, zstring);`
or
`zstring*	zstr = zstring_on_heap(arena, 0, 0);`
