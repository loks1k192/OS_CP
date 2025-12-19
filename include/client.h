#ifndef CLIENT_H
#define CLIENT_H

#define _GNU_SOURCE

// Константы, используемые в клиентских модулях
#define SERVER_FIFO "/tmp/server_fifo"
#define MAX_LOGIN 64
#define MAX_FIFO_PATH 256
#define MAX_MESSAGE 1024

// Глобальные переменные (определены в client_comm.c)
extern char login[MAX_LOGIN];
extern char my_fifo[MAX_FIFO_PATH];
extern int running;

// Прототипы функций
void *reader_thread(void *arg);
int send_to_server(const char *txt);

#endif