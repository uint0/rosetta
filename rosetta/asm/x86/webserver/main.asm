;
; A pretty horrible x86 http responder
;

global _start

section .text

;; syscalls ;;
%define SYS_EXIT 1
%define SYS_READ 3
%define SYS_WRITE 4
%define SYS_CLOSE 6
%define SYS_SOCKET 359
%define SYS_BIND 361
%define SYS_LISTEN 363
%define SYS_ACCEPT4 364
%define SYS_SETSOCKOPT 366

;; net ;;
%define AF_INET 2
%define SOCK_STREAM 1
%define IPPROTO_IP 0
%define SOL_SOCKET 1
%define SO_REUSEADDR 2
%define STDOUT 1

;; constants ;;
%define BIND_HOST 0
; htons(9000) == 10275  (little endian)
%define BIND_PORT 10275
%define MAX_CONN_BACKLOG 10
%define BUFSIZ 4096



; void write_http_headers(int socket, short content_length) // content_length < BUFSIZ
write_http_headers:
    push ebp
    mov ebp, esp

    ; HTTP/1.1 200 OK
    mov ebx, [ebp + 8]
    mov ecx, http_ok
    mov edx, http_ok_len
    mov eax, SYS_WRITE
    int 0x80

    ; Content-Length:
    mov ebx, [ebp + 8]
    mov ecx, content_length_pref
    mov edx, content_length_pref_len
    mov eax, SYS_WRITE
    int 0x80

    ; Write content length
    mov eax, [ebp+12]

    ; Get ready to do a int to dec conversion (int is at most 12 bits (< BUFSIZ))
    mov ecx, esp
    sub esp, 4
    xor edx, edx
    mov ebx, 10

    ; Write characters in order to stack
    cl_itoa_bytes:
        xor edx, edx
        div ebx
        add dl, '0'
        mov byte [ecx - 1], dl
        dec ecx
        cmp eax, 0
        jne cl_itoa_bytes

    ; Write (4 - (ecx - esp)) bytes to the socket. This is itoa(content_length)
    mov ebx, [ebp + 8]
    mov eax, ecx
    sub eax, esp
    mov edx, 4
    sub edx, eax
    mov eax, SYS_WRITE
    int 0x80

    add esp, 4

    ; Write break lines
    mov ebx, [ebp + 8]
    mov ecx, header_end
    mov edx, header_end_len
    mov eax, SYS_WRITE
    int 0x80

    pop ebp
    ret


; void handle_connection(int socket)
handle_connection:
    push ebp
    mov ebp, esp
    mov eax, [ebp + 8]

    ; Allocate a big buffer and read to it
    sub esp, BUFSIZ
    mov ebx, eax
    mov ecx, esp
    mov edx, BUFSIZ
    mov eax, SYS_READ
    int 0x80

    ; Find the offset of the first two spaces
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    find_space:
        mov ebx, [esp + eax]
        
        cmp bl, 0x20  ; ord(' ') 
        jne find_space_incr

        ; Breaks if the first character read is ' '
        mov edx, eax
        cmp ecx, 0
        jg find_space_end
        mov ecx, eax

        find_space_incr:
            inc eax
            cmp eax, BUFSIZ
            jl find_space

            ; When path or method is too long, default them to the length of the buffer
            mov edx, eax
            cmp ecx, 0
            jg find_space_end
            mov ecx, eax
    find_space_end:
    mov eax, ecx
    mov ebx, edx
    ; Calculate the content length into ecx
    ;  Content-Length = method_pref_len + space_offset_1
    ;                 + path_pref_len + (space_offset_2 - space_offset_1 - 1) + len('\n')
    ;  Content-Length = method_pref_len + path_pref_len + space_offset_2
    xor ecx, ecx
    add ecx, method_pref_len
    add ecx, path_pref_len
    add ecx, ebx

    sub ebx, eax
    dec ebx

    ; Push results to the stack
    push ebx
    push eax

    ; Write headers
    push ecx
    push dword [ebp + 8]
    call write_http_headers
    add esp, 8

    ; Write body
    mov ebx, [ebp + 8]

    mov ecx, method_pref
    mov edx, method_pref_len
    mov eax, SYS_WRITE
    int 0x80

    lea ecx, [esp + 8]
    pop edx
    mov eax, SYS_WRITE
    int 0x80

    mov ecx, nl
    mov edx, 1
    mov eax, SYS_WRITE
    int 0x80

    mov ecx, path_pref
    mov edx, path_pref_len
    mov eax, SYS_WRITE
    int 0x80

    lea ecx, [esp + 8]
    pop edx
    mov eax, SYS_WRITE
    int 0x80

    ; Free the buffer
    add esp, BUFSIZ

    ; Close the client socket
    mov ebx, [ebp + 8]
    mov eax, SYS_CLOSE
    int 0x80

    pop ebp
    ret

_start:
    ; Create a socket
    mov ebx, AF_INET
    mov ecx, SOCK_STREAM
    mov edx, IPPROTO_IP
    mov eax, SYS_SOCKET
    int 0x80

    ; Set reuseaddr
    push eax
    mov ebx, dword [esp]
    mov ecx, SOL_SOCKET
    mov edx, SO_REUSEADDR
    push 1
    mov esi, esp
    mov edi, 4
    mov eax, SYS_SETSOCKOPT
    int 0x80
    add esp, 4

    ; Bind it to 0.0.0.0:9000
    ; struct sockaddr_in {
    ;   unsigned short int sa_family;  (:2)
    ;   uint16_t port;                 (:2)
    ;   struct in_addr {               (:4)
    ;     uint32_t s_addr
    ;   } sin_addr;
    ;   8 bytes of padding
    ; }
    ; sizeof(struct sockaddr) == 16
    mov ebx, [esp]
    sub esp, 16  ; sizeof(sockaddr)
    mov BYTE [esp+0], AF_INET
    mov WORD [esp+2], BIND_PORT
    mov DWORD [esp+6], BIND_HOST
    mov ecx, esp
    mov edx, 16
    mov eax, SYS_BIND
    int 0x80
    add esp, 16

    list_loop:
        ; Listen on socket
        mov ebx, dword [esp]
        mov ecx, MAX_CONN_BACKLOG
        mov eax, SYS_LISTEN
        int 0x80

        ; Accept connection
        mov ebx, dword [esp]
        mov ecx, 0
        mov edx, 0
        mov esi, 0
        mov eax, SYS_ACCEPT4
        int 0x80

        ; Handle the connection
        push eax
        call handle_connection
        add esp, 4

        loop list_loop

    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x80

section .data
    http_ok: db "HTTP/1.1 200 OK", 0xd, 0xa
    http_ok_len: equ $-http_ok
    content_length_pref: db "Content-Length:", 0x20
    content_length_pref_len: equ $-content_length_pref
    header_end: db 0xd, 0xa, 0xd, 0xa
    header_end_len: equ $-header_end
    method_pref: db "Method:", 0x20
    method_pref_len: equ $-method_pref
    path_pref: db "Path:", 0x20
    path_pref_len: equ $-path_pref
    nl: db 0xa
