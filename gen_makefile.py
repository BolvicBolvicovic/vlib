#!/usr/bin/python3
import os
import sys

def generate_makefile():
    # 1. Handle Binary Name from argv
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <binary_name>")
        sys.exit(1)

    bin_name = sys.argv[1]
    src_dir = "src"

    # 2. Bootstrap: Create src/ and main.c if missing
    if not os.path.exists(src_dir):
        os.makedirs(src_dir)
        print(f"Created directory: {src_dir}")

    main_path = os.path.join(src_dir, "main.c")
    if not os.path.exists(main_path):
        with open(main_path, "w") as f:
            f.write('#include <stdio.h>\n\nint\nmain(/*int argc, char** argv*/)\n{\n\tprintf("Hello, World!\\n");\n\treturn 0;\n}\n')
        print(f"Generated default: {main_path}")

    # 3. Dynamic Source Discovery
    # We gather all .c files to build the SRCS list
    c_files = [
            os.path.join(src_dir, f) 
            for f in os.listdir(src_dir) 
            if f.endswith('.c') and f != 'main.c'
            ]
    src_list = " ".join(c_files)

    # 4. Construct the Makefile
    makefile_content = f"""BIN={bin_name}
BIN_MAIN={main_path}
TEST=$(BIN)-test
TEST_MAIN=./vlib/vtest.c
CC=gcc
CFLAGS=-Werror -Wextra -Wall
INC=-I./src -I./vlib

SRCS={src_list} $(BIN_MAIN)
OBJS=$(SRCS:.c=.o)

TSRCS={src_list} $(TEST_MAIN)
TOBJS=$(TSRCS:.c=.o)

all: $(BIN)

$(BIN): $(OBJS)
\t$(CC) $(CFLAGS) $(OBJS) -o $@

test: $(TEST)
\t$(CC) $(CFLAGS) -Wno-unused-function $(OBJS) -o $@

$(TEST): $(TOBJS)
\t$(CC) $(CFLAGS) $(TOBJS) -o $@

%.o: %.c
\t$(CC) $(CFLAGS) $(INC) -c $< -o $@

clean:
\trm -rf $(OBJS) $(BIN)

re: clean all

commands:
\tbear -- $(MAKE)

.PHONY: all clean commands re
"""

    with open("Makefile", "w") as f:
        f.write(makefile_content)

    print(f"Successfully generated Makefile for '{bin_name}'.")

if __name__ == "__main__":
    generate_makefile()
