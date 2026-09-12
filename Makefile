CC = cc
CFLAGS = -Wall -Wextra -O2
LIBS = -lX11

TARGET = screentime

all:
	$(CC) $(CFLAGS) screentime.c -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)

install:
	mkdir -p $(HOME)/.local/bin
	cp $(TARGET) $(HOME)/.local/bin/

uninstall:
	rm -f $(HOME)/.local/bin/$(TARGET)
