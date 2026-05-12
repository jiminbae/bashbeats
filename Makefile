CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -pthread -lm
ALSA_CFLAGS := $(shell pkg-config --cflags alsa 2>/dev/null)
ALSA_LIBS := $(shell pkg-config --libs alsa 2>/dev/null)

COMMON_SRC = src/common.c src/wav.c src/session.c src/synth.c src/ring.c
SERVER_SRC = src/server.c $(COMMON_SRC)
CLIENT_SRC = src/client.c src/common.c

ifneq ($(ALSA_LIBS),)
CLIENT_ALSA_FLAGS = -DHAVE_ALSA $(ALSA_CFLAGS)
CLIENT_ALSA_LIBS = $(ALSA_LIBS)
endif

.PHONY: all clean samples run-server run-client
all: bashbeats_server bashbeats_client

bashbeats_server: $(SERVER_SRC) src/*.h
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

bashbeats_client: $(CLIENT_SRC) src/*.h
	$(CC) $(CFLAGS) $(CLIENT_ALSA_FLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS) $(CLIENT_ALSA_LIBS)

samples:
	python3 tools/generate_samples.py

run-server: all samples
	./bashbeats_server samples/kick.wav samples/snare.wav samples/hat.wav samples/piano.wav

run-client: all
	./bashbeats_client 127.0.0.1 7777 --file out.raw

clean:
	rm -f bashbeats_server bashbeats_client out.raw session.daw
