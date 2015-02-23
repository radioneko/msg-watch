CFLAGS := -Wall -Os -g $(shell pkg-config --cflags --libs libnotify)
#CC := clang

msg-watch: msg-watch.c
	$(CC) -o$@ $^ $(CFLAGS) -pthread
