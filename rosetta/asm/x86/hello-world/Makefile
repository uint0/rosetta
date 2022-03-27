all: main

main.o: main.asm
	nasm -f elf32 -o main.o main.asm

main: main.o
	ld -m elf_i386 -o main main.o

clean:
	rm -f main main.o