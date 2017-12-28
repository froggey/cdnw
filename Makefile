all: cdnw.elf

cdnw.elf: entry.o cdnw.o display.o
	gcc -nostdlib -nostartfiles -m32 -Wl,-Ttext=0x100000,--build-id=none -o $@ $^

entry.o: entry.S
	gcc -g -c -m32 -o $@ $^

cdnw.o: cdnw.c cdnw.h
display.o: display.c cdnw.h

%.o: %.c
	gcc -O2 -g -ffreestanding -std=gnu99 -c -m32 -o $@ $<

clean:
	rm -f *.o cdnw.elf

.PHONY: clean
