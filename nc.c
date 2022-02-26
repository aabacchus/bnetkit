/*
 * This file is in the public domain.
 * Author: Ben Fuller
 *
 * Basic netcat-like utility to create a connection on the specified
 * address and port, using stdin and stdout.
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

#define BUF_LEN 1024

void usage(const char *);
int sock_write(int, char *, size_t);
int sock_read(int);

int main(int argc, char *argv[]) {
    int s, sfd = -1;
    struct addrinfo hints, *result, *rp;
    if (argc < 3) {
        usage(argv[0]);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;


    s = getaddrinfo(argv[1], argv[2], &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return 1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        if (close(sfd) == -1) {
            perror("close");
            return 1;
        }
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "connection refused\n");
        return 1;
    }

    int pollret;
    struct pollfd fds[2];
    fds[0].fd = 0;
    fds[0].events = POLLIN;
    fds[1].fd = sfd;
    fds[1].events = POLLIN | POLLWRBAND;

    char *line = NULL;
    size_t linelen = 0;

    while (1) {
        pollret = poll(fds, 2, -1);
        if (pollret <= 0) {
            if (errno == EAGAIN)
                continue;
            /*if (pollret == 0)
                fprintf(stderr, "timed out\n");*/
            perror("poll");
            free(line);
            return 1;
        }
        if (fds[0].revents & POLLIN) {
            ssize_t n;
            if ((n = getline(&line, &linelen, stdin)) == -1) {
                break;
            }
            if (sock_write(sfd, line, (size_t)n) != 0) {
                free(line);
                return 1;
            }
        }
        if (fds[1].revents & POLLIN) {
            int rec = sock_read(sfd);
            if (rec < 0) {
                free(line);
                return 1;
            } else if (rec == 0) {
                break;
            }
        }
        fds[0].revents = 0;
        fds[1].revents = 0;
    }

    free(line);

    if (close(sfd) == -1) {
        perror("close");
        return 1;
    }
    return 0;
}

void usage(const char *argv0){
    fprintf(stderr, "usage: %s host port\n", argv0);
    exit(1);
}

int sock_write(int sfd, char *msg, size_t len){
    ssize_t ret;
    ret = send(sfd, msg, len, 0);
    if (ret != (ssize_t) len) {
        perror("sock_write");
        return 1;
    }
    return 0;
}

int sock_read(int sfd){
    char buff[BUF_LEN];// = {'\0'};
    memset(&buff, 0, BUF_LEN);

    ssize_t ret = recv(sfd, buff, BUF_LEN, 0);

    if (ret < 0) {
        perror("sock_read");
        return -1;
    }
    if (ret == 0)
        return 0;
    write(1, buff, ret);
    return 1;
}
