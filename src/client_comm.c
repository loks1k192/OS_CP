/* client_comm.c — reader thread и отправка команд на сервер */
#define _GNU_SOURCE
#include "../include/client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //unix standart
#include <fcntl.h>  //file control
#include <errno.h>

/* globals (определены здесь) */
char login[MAX_LOGIN];
char my_fifo[MAX_FIFO_PATH];
int running = 1;

void *reader_thread(void *arg)
{
    (void)arg;
    int fd = open(my_fifo, O_RDONLY);
    if (fd < 0)
    {
        perror("open my fifo for read");
        return NULL;
    }
    char buf[2048];
    while (running)
    {
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        if (r > 0)
        {
            buf[r] = '\0';
            printf("\n=== NEW MESSAGE ===\n%s\n> ", buf);
            fflush(stdout);
        }
        else if (r == 0)
        {
            usleep(100000);
        }
        else
        {
            usleep(100000);
        }
    }
    close(fd);
    return NULL;
}

int send_to_server(const char *txt)
{
    int fd = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0)
    {
        fd = open(SERVER_FIFO, O_WRONLY);
        if (fd < 0)
        {
            perror("open server fifo for write");
            return -1;
        }
    }
    ssize_t w = write(fd, txt, strlen(txt));
    (void)w;
    close(fd);
    return 0;
}
