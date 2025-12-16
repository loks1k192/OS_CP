/* server_main.c — main для серверной части (FIFO) */

#define _GNU_SOURCE
#include "../include/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h> /* <-- добавлено для mkfifo */

int main()
{
    // create server fifo if not exists
    unlink(SERVER_FIFO);
    if (mkfifo(SERVER_FIFO, 0666) < 0)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo server");
            return 1;
        }
    }
    // open in read/write to avoid blocking on no writers
    int server_fd = open(SERVER_FIFO, O_RDONLY | O_NONBLOCK);
    if (server_fd < 0)
    {
        perror("open server fifo");
        return 1;
    }
    // also open a write descriptor so that read() doesn't get EOF when no writers
    int dummy_w = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK);

    (void)dummy_w;

    pthread_t sched;
    pthread_create(&sched, NULL, scheduler_thread, NULL);

    printf("Server started. FIFO: %s\n", SERVER_FIFO);

    char buf[2048];
    while (1)
    {
        ssize_t r = read(server_fd, buf, sizeof(buf) - 1);
        if (r > 0)
        {
            buf[r] = '\0';
            // read may contain multiple lines; process each line separately
            char *saveptr = NULL;
            char *line = strtok_r(buf, "\n", &saveptr);
            while (line)
            {
                // copy because process_command uses strtok
                char tmp[2048];
                strncpy(tmp, line, sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = '\0';
                process_command(tmp);
                line = strtok_r(NULL, "\n", &saveptr);
            }
        }
        else
        {
            // no data; sleep a bit
            usleep(100000);
        }
    }

    // cleanup (never reached in this simple demo)
    running = 0;
    pthread_cond_signal(&cond);
    pthread_join(sched, NULL);
    close(server_fd);
    unlink(SERVER_FIFO);
    return 0;
}
