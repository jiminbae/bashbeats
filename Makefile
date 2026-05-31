CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g -Iinclude
LIBS    = -lm -lncurses
TARGET  = bashbeats

SRCS_FULL = src/main.c src/data.c src/audio.c src/stream.c src/file_io.c \
            src/editor.c src/piano.c src/input.c src/perform.c

SRCS_STUB = src/main.c src/data.c src/audio_stub.c src/stream.c src/file_io.c \
            src/editor.c src/piano.c src/input.c src/perform.c

# ── bbeat_client (cross-platform PCM stream receiver) ────────────────
CLIENT_SRC = client/bbeat_client.c
CLIENT_BIN = bbeat_client

UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(OS),Windows_NT)
  CC_CLIENT    = gcc
  CFLAGS_CLIENT = -Wall -Wextra
  LIBS_CLIENT  = -lws2_32 -lwinmm
else ifeq ($(UNAME_S),Darwin)
  CC_CLIENT    = gcc
  CFLAGS_CLIENT = -Wall -Wextra -pthread
  LIBS_CLIENT  = -lpthread -framework AudioToolbox -framework CoreFoundation
else
  CC_CLIENT    = gcc
  CFLAGS_CLIENT = -Wall -Wextra -pthread
  LIBS_CLIENT  = -lpthread
endif

all: full

full:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS_FULL) $(LIBS)
	@echo "Built $(TARGET) with the real audio engine"

stub:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS_STUB) $(LIBS)
	@echo "Built $(TARGET) with audio_stub.c"

client: $(CLIENT_SRC)
	$(CC_CLIENT) $(CFLAGS_CLIENT) -o $(CLIENT_BIN) $(CLIENT_SRC) $(LIBS_CLIENT)
	@echo "Built $(CLIENT_BIN)"

samples:
	python3 tools/generate_samples.py

clean:
	rm -f $(TARGET) $(CLIENT_BIN)

log:
	tail -f /tmp/bashbeats.log

.PHONY: all full stub client samples clean log
