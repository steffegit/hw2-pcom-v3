CC = g++
CFLAGS = -Wall -Werror -Wno-error=unused-variable -g -Iinclude -std=c++17

all: server subscriber

server: server.cpp tcp_protocol.cpp common.cpp
	$(CC) $(CFLAGS) -o $@ $^

subscriber: subscriber.cpp tcp_protocol.cpp common.cpp
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f server subscriber *.o

%.o: %.cpp
	$(CFLAGS) -c $<

.PHONY: all clean
