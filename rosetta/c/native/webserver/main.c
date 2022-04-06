/*
 * An unsafe, incorrect, barely functional http-server i wrote for fun
 *
 * Echos out a formatted version of the request back to the user
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define CONVSTR(x) #x
#define TOSTRING(x) CONVSTR(x)

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define READ_BUFSIZE 2048
#define MAX_CONN_BACKLOG 24

#define HTTP_MAX_METHOD_LENGTH 16
#define HTTP_MAX_PATH_LENGTH 2048
#define HTTP_MAX_VERSION_LENGTH 12
#define HTTP_MAX_HEADERS 256
#define HTTP_MAX_HEADER_KEY_LENGTH 64
#define HTTP_MAX_HEADER_VAL_LENGTH 1024

struct http_header {
    char key[HTTP_MAX_HEADER_KEY_LENGTH];
    char value[HTTP_MAX_HEADER_VAL_LENGTH];
};

struct http_request {
    char method[HTTP_MAX_METHOD_LENGTH];
    char path[HTTP_MAX_PATH_LENGTH];
    char version[HTTP_MAX_VERSION_LENGTH];

    size_t allocated_headers;
    struct http_header headers[HTTP_MAX_HEADERS];
};

// ------ util ------- //

void exit_with_error(const char *msg) {
    perror(msg);
    exit(1);
}

void panic(const char *msg) {
    fputs(msg, stderr);
    exit(255);
}

// ------- networking ------ //

char *format_sockaddr(struct sockaddr *restrict addrinfo, char *buf) {
    switch(addrinfo->sa_family) {
        case AF_INET: {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)addrinfo;
            inet_ntop(AF_INET, &(addr_in->sin_addr), buf, INET_ADDRSTRLEN);
        }; break;
        case AF_INET6: {
            struct sockaddr_in *addr_in6 = (struct sockaddr_in *)addrinfo;
            inet_ntop(AF_INET, &(addr_in6->sin_addr), buf, INET6_ADDRSTRLEN);
        }; break;
        default:
            *buf = 0;
            return NULL;
    }
    return buf;
}

int make_server_socket(const char *port) {
    struct addrinfo info, *r;
    memset(&info, 0, sizeof(info));

    info.ai_family = AF_UNSPEC;
    info.ai_socktype = SOCK_STREAM;
    info.ai_flags = AI_PASSIVE;

    if(getaddrinfo(NULL, port, &info, &r) != 0) exit_with_error("error getaddrinfo()");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) exit_with_error("Could not open socket");

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    if(bind(sock, r->ai_addr, r->ai_addrlen) < 0) exit_with_error("error bind()");

    char log_buf[INET6_ADDRSTRLEN] = {0};
    printf("Listening on %s:%s\n", format_sockaddr(r->ai_addr, log_buf), port);

    freeaddrinfo(r);
    return sock;
}

// ------ http ------- //

size_t write_str(int fd, const char *str) {
    return write(fd, str, strlen(str));
}

size_t write_http_status(int fd, const char *status) {
    size_t n_written = 0;

    n_written += write_str(fd, "HTTP/1.1 ");
    n_written += write_str(fd, status);
    n_written += write_str(fd, "\r\n");
    n_written += write_str(fd, "content-length: 0\r\n");
    n_written += write_str(fd, "\r\n");
    
    return n_written;
}

size_t vwrite_chunk_fmt(int fd, const char *fmt, va_list argp) {
    va_list size_list;
    va_copy(size_list, argp);
    
    size_t n_written = 0;
    size_t bufsiz = vsnprintf(NULL, 0, fmt, size_list) + 1;
    size_t bufsize_hexstr_len = snprintf(NULL, 0, "%lx\r\n", bufsiz) + 1;

    char *contentbuf = calloc(bufsiz, sizeof(char));
    char *sizebuf = calloc(bufsize_hexstr_len, sizeof(char));

    snprintf(sizebuf, bufsize_hexstr_len, "%lx\r\n", bufsiz - 1);
    vsnprintf(contentbuf, bufsiz, fmt, argp);

    n_written += write_str(fd, sizebuf);
    n_written += write_str(fd, contentbuf);
    n_written += write_str(fd, "\r\n");

    va_end(size_list);
    free(sizebuf);
    free(contentbuf);

    return n_written;
}

size_t write_chunk_fmt(int fd, const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    size_t n_written = vwrite_chunk_fmt(fd, fmt, argp);
    va_end(argp);
    return n_written;
}

size_t write_end_chunk(int fd) {
    return write_str(fd, "0\r\n\r\n");
}

char *read_verb_line(int socket, struct http_request *request, const char *buf) {
    int readed = sscanf(
        buf,
        "%" TOSTRING(HTTP_MAX_METHOD_LENGTH) "s "
        "%" TOSTRING(HTTP_MAX_PATH_LENGTH) "s "
        "%" TOSTRING(HTTP_MAX_VERSION_LENGTH) "s",
        request->method,
        request->path,
        request->version
    );
    if(readed != 3) {
        write_http_status(socket, "400 bad request");
        return NULL;
    }
    char *next = strstr(buf, "\r\n");
    return next ? next + 2 : NULL;  // pretty sure this can overflow :shrug:
}

char *read_header_line(int socket, struct http_header *header, const char *buf) {
    int readed = sscanf(
        buf,
        "%" TOSTRING(HTTP_MAX_HEADER_KEY_LENGTH) "[^\r\n:]: "
        "%" TOSTRING(HTTP_MAX_HEADER_VAL_LENGTH) "[^\r\n]",
        header->key,
        header->value
    );
    if(readed != 2) {
        write_http_status(socket, "400 bad request");
        return NULL;
    }
    char *next = strstr(buf, "\r\n");
    if(next) {
        return next + 2;
    } else {
        write_http_status(socket, "400 bad request");
        return NULL;
    }
}

char *read_http_request(struct http_request *request, char *buf, int socket) {
    ssize_t n_read = 0;

    n_read = read(socket, buf, READ_BUFSIZE);
    if(n_read < 0) exit_with_error("error read()");

    char *cursor = buf;

    cursor = read_verb_line(socket, request, cursor);
    if(!cursor) { return NULL; }

    // Theres probably a off by one in here ¯\_(ツ)_/¯
    while((cursor = read_header_line(
        socket,
        &request->headers[request->allocated_headers++],
        cursor
    ))) {
        if(!strncmp(cursor, "\r\n", 2)) {
            return cursor + 2;
        };
    };

    return NULL;
}

void process_http_request(int socket, struct sockaddr conninfo) {
    struct http_request request = {0};
    size_t content_length = 0;
    char *buf = calloc(READ_BUFSIZE, sizeof(*buf));

    char *cursor = read_http_request(&request, buf, socket);

    char log_buf[INET6_ADDRSTRLEN] = {0};
    printf("[%s] %s %s\n", format_sockaddr(&conninfo, log_buf), request.method, request.path);

    write_str(socket,
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
    );
    write_chunk_fmt(socket, "Type: %s\n", request.method);
    write_chunk_fmt(socket, "Path: %s\n", request.path);

    write_chunk_fmt(socket, "Headers:\n");
    for(size_t i = 0; i < request.allocated_headers; i++) {
        if(!strncasecmp(request.headers[i].key, "content-length", strlen("content-length"))) {
            content_length = strtoul(request.headers[i].value, NULL, 10);
        }
        write_chunk_fmt(socket, "  Key: %s, Value: %s\n", request.headers[i].key, request.headers[i].value);
    }

    write_chunk_fmt(socket, "Body:\n");
    if(cursor) {
        write_chunk_fmt(socket, "%s", cursor);
    }
    ssize_t left_to_read = content_length - strlen(cursor);
    while(left_to_read > 0) {
        ssize_t n_read = read(socket, buf, MIN(READ_BUFSIZE, left_to_read));
        if(n_read <= 0) { break; }  // Best effort

        left_to_read -= n_read;
        write_chunk_fmt(socket, "%s", buf);
        memset(buf, 0, READ_BUFSIZE);
    }

    write_end_chunk(socket);
    free(buf);
}

// ------ server ------- //

int serve_forever(const char *port) {
    int listen_socket = make_server_socket(port);

    for(;;) {
        struct sockaddr conninfo;
        socklen_t info_len = sizeof(conninfo); 

        listen(listen_socket, MAX_CONN_BACKLOG);
        int conn = accept(listen_socket, &conninfo, &info_len);

        process_http_request(conn, conninfo);

        close(conn);
    }
}

int main(void) {
    serve_forever("9000");
}
