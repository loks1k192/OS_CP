#ifndef SERVER_H
#define SERVER_H

#define _GNU_SOURCE
#include <pthread.h>
#include <time.h>

#define SERVER_FIFO "/tmp/server_fifo"
#define MAX_LOGIN 64
#define MAX_FIFO_PATH 256
#define MAX_MESSAGE 1024

typedef struct Client
{
    char login[MAX_LOGIN];
    char fifo_path[MAX_FIFO_PATH];
    int online;
    struct Client *next;
} Client;

typedef struct Message
{
    char from[MAX_LOGIN];
    char to[MAX_LOGIN];
    char text[MAX_MESSAGE];
    time_t deliver_time;
    struct Message *next;
} Message;

// Глобальные переменные
extern Client *clients;
extern Message *messages;
extern pthread_mutex_t mtx;
extern pthread_cond_t cond;
extern int running;

// Прототипы функций для server_api
Client *find_client_locked(const char *login);
void register_client_locked(const char *login, const char *fifo_path);
void unregister_client_locked(const char *login);

void push_message_locked(Message *m);
Message *pop_earliest_locked(void);
Message *pop_specific_for_recipient_locked(const char *to);

void try_deliver_message(Message *m);
void *scheduler_thread(void *arg);
void deliver_pending_for_client_locked(const char *login);

void process_command(char *line);

#endif
