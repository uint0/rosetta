global _start

section .text

%define SYS_WRITE     4
%define SYS_EXIT      1
%define STDOUT_FILENO 1

_start:
    mov eax, SYS_WRITE
    mov ebx, STDOUT_FILENO
    mov ecx, message
    mov edx, length
    int 0x80


    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x80


section .data
    message: db "Hello world!", 0xa
    length: equ $-message