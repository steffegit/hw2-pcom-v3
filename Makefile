CC = g++
CFLAGS = -Wall -Werror -Wno-error=unused-variable -g -Iinclude -std=c++17 -O2

all: server subscriber

server: server.cpp lib/tcp_protocol.cpp lib/common.cpp
	$(CC) $(CFLAGS) -o $@ $^

subscriber: subscriber.cpp lib/tcp_protocol.cpp lib/common.cpp
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f server subscriber *.o

%.o: %.cpp
	$(CFLAGS) -c $<

.PHONY: all clean
