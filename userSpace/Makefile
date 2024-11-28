###################################################################
#NOTE: doing 2 seperate builds because of main functions.         #
###################################################################

# Add the names of your .c files from your assignment here.
MKMEMEFS_SRCS = mkmemefs.c
MEMEFS_SRCS = memefs.c

# This line is the name of the program you are building
MKMEMEFS_BIN = mkmemefs
MEMEFS_BIN = memefs

# Compiler flags to pass to the system's C compiler while building
# the source files
CFLAGS = -g -Wall -Wextra -pedantic -Werror -std=gnu11

# Flags to pass to the linker while linking your program
# You shouldn't need to change this at all.
LDFLAGS = -g

###################################################################
# You shouldn't need to touch anything below this line...         #
###################################################################

MKMEMEFS_OBJS = $(MKMEMEFS_SRCS:.c=.o)
MEMEFS_OBJS = $(MEMEFS_SRCS:.c=.o)

all: $(MKMEMEFS_BIN) $(MEMEFS_BIN)

# Build mkmemefs
$(MKMEMEFS_BIN): $(MKMEMEFS_OBJS)
	$(CC) -o $@ $(MKMEMEFS_OBJS) $(LDFLAGS)

# Build memefs
$(MEMEFS_BIN): $(MEMEFS_OBJS)
	$(CC) -o $@ $(MEMEFS_OBJS) $(LDFLAGS) -lfuse3

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(MKMEMEFS_OBJS) $(MEMEFS_OBJS)
	rm -f $(MKMEMEFS_BIN) $(MEMEFS_BIN)
