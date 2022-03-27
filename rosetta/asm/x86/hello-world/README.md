# x86 hello world

```shell-session
$ nasm -f elf32 -o main.o main.asm
$ ld -m elf_i386 -o main main.o
```
