CC = gcc
CFLAGS = -O3 -Wall -Wextra
TARGET: lc3-vm

all: $(TARGET)

lc3-vm: lc3-vm.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f lc3-vm *.o
