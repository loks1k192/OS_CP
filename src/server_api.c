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

/* Глобальные переменные (изначально статические в монолитной версии) */
Client *clients = NULL;
Message *messages = NULL; // ожидающие + запланированные сообщения
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
    // вставка с сортировкой по возрастанию времени доставки
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
        // пытаемся открыть FIFO получателя
        int fd = open(rcpt->fifo_path, O_WRONLY | O_NONBLOCK);
        if (fd >= 0)
        {
            ssize_t w = write(fd, line, strlen(line));
            (void)w;
            close(fd);
            // доставлено
            free(m);
            pthread_mutex_unlock(&mtx);
            return;
        }
        else
        {
            // не удалось открыть (нет читателя) => оставляем сообщение в ожидании
            // оставляем сообщение в очереди ожидания
        }
    }
    // получатель оффлайн или не удалось открыть FIFO -> перепланируем на будущее
    push_message_locked(m);
    pthread_mutex_unlock(&mtx);
}

void *scheduler_thread(void *arg)
{
    (void)arg;
    while (running)
    {
        pthread_mutex_lock(&mtx);
        // находим самое раннее сообщение
        if (!messages)
        {
            // ждём, пока не появится новое сообщение
            pthread_cond_wait(&cond, &mtx);
            pthread_mutex_unlock(&mtx);
            continue;
        }
        time_t now = time(NULL);
        Message *m = messages;
        if (m->deliver_time > now)
        {
            // ожидаем до m->deliver_time или до вставки нового сообщения
            struct timespec ts;
            ts.tv_sec = m->deliver_time;
            ts.tv_nsec = 0;
            int rc = pthread_cond_timedwait(&cond, &mtx, &ts);
            (void)rc;
            // возвращаемся к переоценке
            pthread_mutex_unlock(&mtx);
            continue;
        }
        // доставляем все сообщения, время которых <= текущего
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
    // находим сообщения для этого логина, извлекаем их и пытаемся доставить
    Message *collected = NULL;
    Message *prev = NULL, *p = messages;
    while (p)
    {
        if (strcmp(p->to, login) == 0)
        {
            Message *next = p->next;
            // удаляем p из messages
            if (prev)
                prev->next = next;
            else
                messages = next;
            // добавляем в collected (без сортировки)
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
    // пытаемся доставить каждое немедленно
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
    // Команды:
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
            fprintf(stderr, "REGISTER требует логин и fifo_path\n");
            return;
        }
        pthread_mutex_lock(&mtx);
        register_client_locked(login, fifo);
        // сигнализируем планировщику на случай существования ожидающих сообщений
        pthread_cond_signal(&cond);
        // немедленно доставляем все ожидающие сообщения для этого клиента
        deliver_pending_for_client_locked(login);
        pthread_mutex_unlock(&mtx);
        printf("Зарегистрирован %s -> %s\n", login, fifo);
    }
    else if (strcmp(cmd, "UNREGISTER") == 0)
    {
        char *login = strtok(NULL, " \t\n");
        if (!login)
            return;
        pthread_mutex_lock(&mtx);
        unregister_client_locked(login);
        pthread_mutex_unlock(&mtx);
        printf("Разрегистрирован %s\n", login);
    }
    else if (strcmp(cmd, "SEND") == 0)
    {
        char *from = strtok(NULL, " \t\n");
        char *to = strtok(NULL, " \t\n");
        char *delay_s = strtok(NULL, " \t\n");
        char *rest = strtok(NULL, "\n"); // остаток = сообщение (может содержать пробелы)
        if (!from || !to || !delay_s)
        {
            fprintf(stderr, "SEND требует from to delay [message]\n");
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
            // rest может начинаться с пробела; обрезаем начальные пробелы
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
        printf("Запланировано сообщение от %s к %s через %ld сек: \"%s\"\n", from, to, delay, m->text);
    }
    else
    {
        fprintf(stderr, "Неизвестная команда: %s\n", cmd);
    }
}