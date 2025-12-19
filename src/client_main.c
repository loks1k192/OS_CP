/* client_main.c — основной клиент (создаёт fifo, регистрируется и взаимодействует) */
#define _GNU_SOURCE
#include "../include/client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>   /* unlink */
#include <sys/stat.h> /* mkfifo */
#include <errno.h>    /* errno, EEXIST */

int main()
{
    printf("Enter your login: ");
    if (!fgets(login, sizeof(login), stdin))
        return 0;

    // перенос на новую строку
    login[strcspn(login, "\n")] = '\0';
    if (strlen(login) == 0)
        return 0;

    snprintf(my_fifo, sizeof(my_fifo), "/tmp/%s.fifo", login);
    // удаляет прошлую fifo, если есть
    unlink(my_fifo);
    if (mkfifo(my_fifo, 0666) < 0)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo my fifo");
            return 1;
        }
    }

    // регистрация на сервере
    char cmd[1500];
    snprintf(cmd, sizeof(cmd), "REGISTER %s %s\n", login, my_fifo);
    if (send_to_server(cmd) < 0)
    {
        fprintf(stderr, "Failed to register on server. Make sure server is running.\n");
    }

    pthread_t reader;
    if (pthread_create(&reader, NULL, reader_thread, NULL) != 0)
    {
        perror("pthread_create");
    }

    printf("Commands:\n");
    printf(" send <to> <delay_seconds> <message...>\n");
    printf(" exit\n");

    char line[2048];
    while (1)
    {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "exit", 4) == 0)
        {
            break;
        }
        else if (strncmp(line, "send ", 5) == 0)
        {
            /* format: send <to> <delay_seconds> <message...> */
            char *p = line + 5;
            char *to = strtok(p, " \t");
            char *delay_s = strtok(NULL, " \t");
            char *msg = strtok(NULL, "");
            if (!to || !delay_s)
            {
                printf("Usage: send <to> <delay_seconds> <message...>\n");
                continue;
            }
            if (!msg)
                msg = "";
            char cmd2[2048];
            /* Compose SEND <from> <to> <delay> <message>\n */
            snprintf(cmd2, sizeof(cmd2), "SEND %s %s %s %s\n", login, to, delay_s, msg);
            if (send_to_server(cmd2) < 0)
            {
                printf("Failed to send command to server.\n");
            }
            else
            {
                printf("Sent.\n");
            }
        }
        else
        {
            printf("Unknown command. Try 'send' or 'exit'.\n");
        }
    }

    /* unregister and cleanup */
    running = 0;
    snprintf(cmd, sizeof(cmd), "UNREGISTER %s\n", login);
    send_to_server(cmd);
    /* remove fifo */
    unlink(my_fifo);
    /* reader thread may be blocked; cancel and join */
    pthread_cancel(reader);
    pthread_join(reader, NULL);
    printf("Client exit.\n");
    return 0;
}
