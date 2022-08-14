TRGT = samsung-book-support
SRCS = $(TRGT).c
OBJS = $(SRCS:.c=.o)
PKGS = glib-2.0 gio-2.0

CFLAGS += `pkg-config --cflags $(PKGS)` -g3
LDFLAGS += `pkg-config --libs $(PKGS)`

$(TRGT): $(OBJS)

clean:
	rm -rf $(TRGT)
	rm -rf $(OBJS)