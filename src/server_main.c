/* server_main.c — основной файл для серверной части (FIFO) */

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
    // Создаём серверный FIFO, если он не существует
    unlink(SERVER_FIFO);
    if (mkfifo(SERVER_FIFO, 0666) < 0)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo server");
            return 1;
        }
    }
    // Открываем на чтение/запись, чтобы избежать блокировки при отсутствии писателей
    int server_fd = open(SERVER_FIFO, O_RDONLY | O_NONBLOCK);
    if (server_fd < 0)
    {
        perror("open server fifo");
        return 1;
    }
    // Также открываем дескриптор для записи, чтобы read() не получал EOF при отсутствии писателей
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
            // В прочитанных данных может быть несколько строк; обрабатываем каждую отдельно
            char *saveptr = NULL;
            char *line = strtok_r(buf, "\n", &saveptr);
            while (line)
            {
                // Копируем, потому что process_command использует strtok
                char tmp[2048];
                strncpy(tmp, line, sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = '\0';
                process_command(tmp);
                line = strtok_r(NULL, "\n", &saveptr);
            }
        }
        else
        {
            // Нет данных; небольшая пауза
            usleep(100000);
        }
    }

    // Очистка
    running = 0;
    pthread_cond_signal(&cond);
    pthread_join(sched, NULL);
    close(server_fd);
    unlink(SERVER_FIFO);
    return 0;
}