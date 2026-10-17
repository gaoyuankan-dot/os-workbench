SRCS := start.S main.c
# -fno-strict-aliasing: main.c fills the ELF header with uint32_t stores but
# reads it through Elf32/Elf64 structs. Modern GCC (seen with GCC 15) treats
# that as non-aliasing and reads e_machine BEFORE the disk read, so a 64-bit
# kernel is mistaken for 32-bit and never loaded (QEMU then runs zeros).
bootblock.o: $(SRCS) Makefile
	@echo + CC $(SRCS)
	@$(CROSS_COMPILE)gcc -static -m32 -fno-pic -Os -fno-strict-aliasing -nostdlib -Ttext 0x7c00 -I$(AM_HOME)/am/src -o bootblock.o $(SRCS)
	@python3 genboot.py bootblock.o

clean:
	rm -rf *.o
