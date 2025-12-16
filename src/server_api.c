/* server_api.c
   Перенос логики сервера из одного файла — все функции и переменные как в оригинале.
*/
#define _GNU_SOURCE
#include "../include/server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

/* Globals (originally static in monolith) */
Client *clients = NULL;
Message *messages = NULL; // pending + scheduled
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int running = 1;

Client *find_client_locked(const char *login)
{
    Client *p = clients;
    while (p)
    {
        if (strcmp(p->login, login) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

void register_client_locked(const char *login, const char *fifo_path)
{
    Client *c = find_client_locked(login);
    if (!c)
    {
        c = calloc(1, sizeof(Client));
        strncpy(c->login, login, MAX_LOGIN - 1);
        c->next = clients;
        clients = c;
    }
    strncpy(c->fifo_path, fifo_path, MAX_FIFO_PATH - 1);
    c->online = 1;
}

void unregister_client_locked(const char *login)
{
    Client *c = find_client_locked(login);
    if (c)
    {
        c->online = 0;
        c->fifo_path[0] = '\0';
    }
}

void push_message_locked(Message *m)
{
    // insert sorted by deliver_time ascending
    if (!messages || m->deliver_time < messages->deliver_time)
    {
        m->next = messages;
        messages = m;
        return;
    }
    Message *p = messages;
    while (p->next && p->next->deliver_time <= m->deliver_time)
        p = p->next;
    m->next = p->next;
    p->next = m;
}

Message *pop_earliest_locked(void)
{
    if (!messages)
        return NULL;
    Message *m = messages;
    messages = messages->next;
    m->next = NULL;
    return m;
}

Message *pop_specific_for_recipient_locked(const char *to)
{
    Message *prev = NULL, *p = messages;
    while (p)
    {
        if (strcmp(p->to, to) == 0)
        {
            if (prev)
                prev->next = p->next;
            else
                messages = p->next;
            p->next = NULL;
            return p;
        }
        prev = p;
        p = p->next;
    }
    return NULL;
}

void try_deliver_message(Message *m)
{
    pthread_mutex_lock(&mtx);
    Client *rcpt = find_client_locked(m->to);
    char line[MAX_MESSAGE + 200];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(line, sizeof(line), "[%s] From: %s\n%s\n\n", timestr, m->from, m->text);

    if (rcpt && rcpt->online && rcpt->fifo_path[0])
    {
        // try to open recipient FIFO
        int fd = open(rcpt->fifo_path, O_WRONLY | O_NONBLOCK);
        if (fd >= 0)
        {
            ssize_t w = write(fd, line, strlen(line));
            (void)w;
            close(fd);
            // delivered
            free(m);
            pthread_mutex_unlock(&mtx);
            return;
        }
        else
        {
            // cannot open (no reader) => keep message pending
            // leave message in pending queue
        }
    }
    // recipient offline or couldn't open FIFO -> requeue for future
    push_message_locked(m);
    pthread_mutex_unlock(&mtx);
}

void *scheduler_thread(void *arg)
{
    (void)arg;
    while (running)
    {
        pthread_mutex_lock(&mtx);
        // find earliest message
        if (!messages)
        {
            // wait until new message arrives
            pthread_cond_wait(&cond, &mtx);
            pthread_mutex_unlock(&mtx);
            continue;
        }
        time_t now = time(NULL);
        Message *m = messages;
        if (m->deliver_time > now)
        {
            // timed wait until m->deliver_time or until a new message inserted
            struct timespec ts;
            ts.tv_sec = m->deliver_time;
            ts.tv_nsec = 0;
            int rc = pthread_cond_timedwait(&cond, &mtx, &ts);
            (void)rc;
            // loop to re-evaluate
            pthread_mutex_unlock(&mtx);
            continue;
        }
        // deliver all messages whose time <= now
        while (messages && messages->deliver_time <= now)
        {
            Message *to_deliver = pop_earliest_locked();
            pthread_mutex_unlock(&mtx);
            try_deliver_message(to_deliver);
            pthread_mutex_lock(&mtx);
            now = time(NULL);
        }
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

void deliver_pending_for_client_locked(const char *login)
{
    // find messages for this login, take them out and try delivering
    Message *collected = NULL;
    Message *prev = NULL, *p = messages;
    while (p)
    {
        if (strcmp(p->to, login) == 0)
        {
            Message *next = p->next;
            // remove p from messages
            if (prev)
                prev->next = next;
            else
                messages = next;
            // push to collected list (unsorted)
            p->next = collected;
            collected = p;
            p = next;
        }
        else
        {
            prev = p;
            p = p->next;
        }
    }
    pthread_mutex_unlock(&mtx);
    // try deliver each immediately
    Message *q = collected;
    while (q)
    {
        Message *next = q->next;
        try_deliver_message(q);
        q = next;
    }
    pthread_mutex_lock(&mtx);
}

void process_command(char *line)
{
    // Commands:
    // REGISTER <login> <fifo_path>
    // UNREGISTER <login>
    // SEND <from> <to> <delay_seconds> <message...>
    char *cmd = strtok(line, " \t\n");
    if (!cmd)
        return;
    if (strcmp(cmd, "REGISTER") == 0)
    {
        char *login = strtok(NULL, " \t\n");
        char *fifo = strtok(NULL, " \t\n");
        if (!login || !fifo)
        {
            fprintf(stderr, "REGISTER requires login and fifo_path\n");
            return;
        }
        pthread_mutex_lock(&mtx);
        register_client_locked(login, fifo);
        // signal scheduler in case pending messages exist
        pthread_cond_signal(&cond);
        // deliver any pending for this client immediately
        deliver_pending_for_client_locked(login);
        pthread_mutex_unlock(&mtx);
        printf("Registered %s -> %s\n", login, fifo);
    }
    else if (strcmp(cmd, "UNREGISTER") == 0)
    {
        char *login = strtok(NULL, " \t\n");
        if (!login)
            return;
        pthread_mutex_lock(&mtx);
        unregister_client_locked(login);
        pthread_mutex_unlock(&mtx);
        printf("Unregistered %s\n", login);
    }
    else if (strcmp(cmd, "SEND") == 0)
    {
        char *from = strtok(NULL, " \t\n");
        char *to = strtok(NULL, " \t\n");
        char *delay_s = strtok(NULL, " \t\n");
        char *rest = strtok(NULL, "\n"); // remainder = message (may contain spaces)
        if (!from || !to || !delay_s)
        {
            fprintf(stderr, "SEND requires from to delay [message]\n");
            return;
        }
        long delay = strtol(delay_s, NULL, 10);
        if (delay < 0)
            delay = 0;
        Message *m = calloc(1, sizeof(Message));
        strncpy(m->from, from, MAX_LOGIN - 1);
        strncpy(m->to, to, MAX_LOGIN - 1);
        if (rest)
        {
            // rest may begin with space; trim leading spaces
            while (*rest == ' ' || *rest == '\t')
                rest++;
            strncpy(m->text, rest, MAX_MESSAGE - 1);
        }
        else
            m->text[0] = '\0';
        m->deliver_time = time(NULL) + delay;
        pthread_mutex_lock(&mtx);
        push_message_locked(m);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mtx);
        printf("Scheduled message from %s to %s in %ld sec: \"%s\"\n", from, to, delay, m->text);
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", cmd);
    }
}