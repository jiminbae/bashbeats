CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g -Iinclude
LIBS    = -lm -lncurses
TARGET  = bashbeats

SRCS_FULL = src/main.c src/data.c src/audio.c src/stream.c src/file_io.c \
            src/editor.c src/piano.c src/input.c src/perform.c

SRCS_STUB = src/main.c src/data.c src/audio_stub.c src/stream.c src/file_io.c \
            src/editor.c src/piano.c src/input.c src/perform.c

all: full

full:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS_FULL) $(LIBS)
	@echo "Built $(TARGET) with the real audio engine"

stub:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS_STUB) $(LIBS)
	@echo "Built $(TARGET) with audio_stub.c"

samples:
	python3 tools/generate_samples.py

clean:
	rm -f $(TARGET)

log:
	tail -f /tmp/bashbeats.log

.PHONY: all full stub samples clean log
