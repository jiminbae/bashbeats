CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
LIBS   = -lm -lasound -lncurses
TARGET = bashbeats

# Full build (audio engine implemented)
SRCS_FULL = main.c data.c audio.c sampler.c \
            stream.c file_io.c editor.c piano.c input.c perform.c

# Stub build (week 1: audio engine not yet implemented)
SRCS_STUB = main.c data.c audio_stub.c \
            stream.c file_io.c editor.c piano.c input.c perform.c

# Default: stub build for independent development
all: stub

stub:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS_STUB) -lm -lncurses -lpthread
	@echo "Built with audio_stub.c — debug output goes to /tmp/bashbeats.log"

full:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS_FULL) $(LIBS)

clean:
	rm -f $(TARGET)

# View audio stub debug log (run in another terminal)
log:
	tail -f /tmp/bashbeats.log

.PHONY: all stub full clean log
