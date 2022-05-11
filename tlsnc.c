/*
 * This file is in the public domain. If this is not possible in your region,
 * please refer to the UNLICENSE which should be included with the software.
 * Author: Ben Fuller
 * Upstream: https://git.bvnf.space/bnetkit/
 *
 * Basic TLS-enabled netcat-like utility to create a connection on the specified
 * address and port, using stdin and stdout.
 *
 * Needs libtls.
 *
 */
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <tls.h>

#define BUF_LEN 1024

int sock_write(struct tls *ctx, char *msg, size_t len);
int sock_read(struct tls *ctx);

int server_connect(char *argv0, char *nodename, char *servname) {
    int sfd = -1;
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    int s = getaddrinfo(nodename, servname, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "%s: getaddrinfo: %s\n", argv0, gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        if (close(sfd) == -1) {
            perror("close");
            freeaddrinfo(result);
            return -1;
        }
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "%s: %s:%s: connection refused\n", argv0, nodename, servname);
        return -1;
    }

    return sfd;
}

int main(int argc, char **argv) {
    int timeout = 5 * 1000; /* 5 seconds */
    if (argc < 3) {
        fprintf(stderr, "usage: %s host port\n", argv[0]);
        return 1;
    }

    struct tls_config *cfg = tls_config_new();
    const char *def_ca_file = tls_default_ca_cert_file();
    tls_config_set_ca_file(cfg, def_ca_file);
    tls_config_insecure_noverifycert(cfg);
    struct tls *ctx = tls_client();
    if (ctx == NULL) {
        fprintf(stderr, "tls client creation failed\n");
        tls_config_free(cfg);
        return 1;
    }
    if (tls_configure(ctx, cfg) == -1) {
        fprintf(stderr, "tls_configure: %s\n", tls_error(ctx));
        tls_config_free(cfg);
        tls_free(ctx);
        return 1;
    }
    tls_config_free(cfg);

    int sfd = server_connect(argv[0], argv[1], argv[2]);
    if (sfd == -1)
        return 1;
    if (tls_connect_socket(ctx, sfd, argv[1]) == -1) {
        fprintf(stderr, "tls_connect_socket: %s\n", tls_error(ctx));
        return 1;
    }

    int pollret;
    struct pollfd fds[2];
    fds[0].fd = 0;
    fds[0].events = POLLIN;
    fds[1].fd = sfd;
    fds[1].events = POLLIN;

    char *line = NULL;
    size_t linelen = 0;

    while (1) {
        fds[0].revents = 0;
        fds[1].revents = 0;
        errno = 0;
        pollret = poll(fds, 2, timeout);
        if (pollret < 0) {
            if (errno == EAGAIN)
                continue;
            perror("poll");
            free(line);
            return 1;
        } else if (pollret == 0) {
            /* timeout */
            fprintf(stderr, "%s: %s:%s: timeout\r\n", argv[0], argv[1], argv[2]);
            free(line);
            return 1;
        }
        if (fds[0].revents) {
            ssize_t n = getline(&line, &linelen, stdin);
            if (n  == -1) {
                /* send EOF to server */
                //if (shutdown(sfd, SHUT_WR) == -1)
                    //perror("shutdown");

                /* prevent poll from checking stdin all the time */
                fds[0].fd = -1;
                continue;
            }
            ssize_t ret = tls_write(ctx, line, (size_t)n);
            if (ret == -1) {
                fprintf(stderr, "sock_write: %s\n", tls_error(ctx));
                tls_free(ctx);
                free(line);
                return 1;
            } else if (ret == TLS_WANT_POLLIN)
                fds[1].events = POLLIN;
            else if (ret == TLS_WANT_POLLOUT)
                fds[1].events = POLLOUT;
        }
        if (fds[1].revents) {
            char buff[BUF_LEN] = {'\0'};
            ssize_t ret = tls_read(ctx, buff, BUF_LEN);

            if (ret == -1) {
                fprintf(stderr, "tls_read: %s\n", tls_error(ctx));
                free(line);
                return 1;
            } else if (ret == TLS_WANT_POLLIN)
                fds[1].events = POLLIN;
            else if (ret == TLS_WANT_POLLOUT)
                fds[1].events = POLLOUT;
            else if (ret == 0) /* received EOF */
                break;
            else {
                write(1, buff, ret);
            }
        }
    }

    free(line);

    if (tls_close(ctx) == -1)
        fprintf(stderr, "tls_close: %s\n", tls_error(ctx));
    tls_free(ctx);
    if (close(sfd) == -1) {
        perror("close");
        return 1;
    }
    return 0;
}

int sock_write(struct tls *ctx, char *msg, size_t len) {
    ssize_t ret = tls_write(ctx, msg, len);
    if (ret != (ssize_t) len) {
        fprintf(stderr, "sock_write: %s\n", tls_error(ctx));
        return 1;
    }
    return 0;
}

int sock_read(struct tls *ctx) {
    char buff[BUF_LEN] = {'\0'};
    ssize_t ret = tls_read(ctx, buff, BUF_LEN);

    if (ret < 0) {
        fprintf(stderr, "sock_read: %s\n", tls_error(ctx));
        return -1;
    }
    if (ret == 0)
        return 0;
    write(1, buff, ret);
    return 1;
}
