# firmware/Makefile

AS = as
LD = ld

all: boot.bin

boot.o: boot.s
	$(AS) --32 -o boot.o boot.s

boot.bin: boot.o
	$(LD) -m elf_i386 -Ttext=0x0000 --oformat binary -o boot.bin boot.o

clean:
	rm -f boot.o boot.bin