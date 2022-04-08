default: main
tiny: main_tiny

main.o: main.asm
	nasm -f elf32 -o main.o main.asm

main: main.o
	ld -m elf_i386 -o main main.o

main_tiny.asm: main.asm
	sed '/^section .data/d' main.asm > main_tiny.asm

main_tiny.o: main_tiny.asm
	nasm -f elf32 -o main_tiny.o main_tiny.asm

main_tiny: main_tiny.o
	ld.gold -m elf_i386 -o main_tiny main_tiny.o

clean:
	rm -f main_tiny.*
	rm -f main.o

clean_all: clean
	rm -rf main main_tiny