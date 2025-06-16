#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "devutils.h"

typedef enum {
    WHITE,
    RED,
    GREEN,
    BLUE,
    YELLOW,
} Color;

void set_color(Color c) {
    switch (c) {
        case WHITE: 
            printf("\x1b[0;37m");
            break;
        case RED:
            printf("\x1b[0;31m");
            break;
        case GREEN:
            printf("\x1b[0;32m");
            break;
        case BLUE:
            printf("\x1b[0;34m");
            break;
        case YELLOW:
            printf("\x1b[0;33m");
            break;
    }
}

void reset_color() {
    printf("\x1b[0m");
}

#define FIFO_NAME "input-fifo"

int make_and_open_fifo() {
    if (mkfifo(FIFO_NAME, 0666) < 0) {
        printf("[ERROR] Could not make fifo '%s': %s\n", FIFO_NAME, strerror(errno));
        // TODO: Maybe don't kill the program just the current Task
        exit(1);
    }
    int fd = open(FIFO_NAME, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("[ERROR] Could not open file '%s': %s\n", FIFO_NAME, strerror(errno));
        // TODO: Maybe don't kill the program just the current Task
        exit(1);
    }
    return fd;
}

void close_and_unlink_fifo(int fd) {
    if (close(fd) < 0) {
        printf("[ERROR] Could not close file: %s\n", strerror(errno));
        // TODO: Maybe don't kill the program just the current Task
        exit(1);
    }
    if (unlink(FIFO_NAME) < 0) {
        printf("[ERROR] Could not unlink file: %s\n", strerror(errno));
        // TODO: Maybe don't kill the program just the current Task
        exit(1);
    }
}

typedef enum {
    STATE_DONE,
    STATE_PENDING,
} State;

typedef enum {
    RESULT_KIND_VOID,
    RESULT_KIND_COMMAND,
    RESULT_KIND_FILE_DESCRIPTOR,
} Result_Kind;

typedef struct {
    State state;
    Result_Kind kind;
    char *command;
    int fd;
} Result;

#define RESULT_DONE    (Result) {.state = STATE_DONE, .kind = RESULT_KIND_VOID}
#define RESULT_PENDING (Result) {.state = STATE_PENDING, .kind = RESULT_KIND_VOID}

typedef enum {
    TASK_KIND_SEQUENCE,
    TASK_KIND_PARALLEL,
    TASK_KIND_THEN,
    TASK_KIND_WAIT,
    TASK_KIND_LOG,
    TASK_KIND_CONTEXT_FIFO,
    TASK_KIND_READ_FIFO,
    TASK_KIND_COUNTER,
} Task_Kind;

typedef struct Task Task;

struct Task {
    Task_Kind kind;

    // TASK_KIND_COUNTER
    int cur;
    int end;
    char *name;
    Color color;

    // TASK_KIND_SEQUENCE
    size_t seq_count;
    struct Task **seq;

    // TASK_KIND_PARALLEL
    size_t par_count;
    size_t par_index;
    Task **par;

    // TASK_KIND_THEN
    Task *fst;
    Task *snd;
    Task *(*func)(Result);

    // TASK_KIND_LOG
    char *message;
    
    // TASK_KIND_WAIT
    double duration;
    bool started;
    time_t start;

    // TASK_KIND_*_FIFO
    int input_fd;

    // TASK_KIND_CONTEXT_FIFO
    Task *body;
    Task *(*build_body)(Result);
};

#define TASK_POOL_CAPACITY 8
Task task_pool[TASK_POOL_CAPACITY];
typedef struct Task_Free_Node Task_Free_Node;
struct Task_Free_Node {
    Task_Free_Node *next;
};
static_assert(sizeof(Task_Free_Node) <= sizeof(Task));
Task_Free_Node *task_pool_head = NULL;

void task_free_all() {
    task_pool_head = NULL;
    for (size_t i=0; i<TASK_POOL_CAPACITY; i++) {
        Task_Free_Node *cur = (Task_Free_Node *) &task_pool[i];
        cur->next = task_pool_head;
        task_pool_head = cur;
    }
    assert(task_pool_head != NULL);
}

Task *task_alloc() {
    Task_Free_Node *cur = task_pool_head;
    if (cur == NULL) {
        UNIMPLEMENTED("alloc_task");
    }
    task_pool_head = cur->next;
    return (Task *) cur;
}

void task_free(Task *t) {
    UNUSED(t);
    UNIMPLEMENTED("free_Task");
}

#define READ_BUF_CAPACITY 32
char read_buf[READ_BUF_CAPACITY];

Result poll(Task *t) {
    switch (t->kind) {
        case TASK_KIND_SEQUENCE: 
            {
                if (t->seq_count == 0) return RESULT_DONE;
                Result r = poll(t->seq[0]);
                switch (r.state) {
                    case STATE_DONE:
                        t->seq_count--;
                        t->seq++;
                    case STATE_PENDING:
                }
                return RESULT_PENDING;
            }
        case TASK_KIND_PARALLEL:
            {
                if (t->par_count == 0) return RESULT_DONE;
                Result r = poll(t->par[t->par_index]);
                switch (r.state) {
                    case STATE_DONE:
                        t->par[t->par_index] = t->par[t->par_count - 1];
                        t->par_count--;
                        if (t->par_count > 0) t->par_index %= t->par_count;
                        break;
                    case STATE_PENDING:
                        t->par_index += 1;
                        t->par_index %= t->par_count;
                        break;
                }
                return RESULT_PENDING;
            }
        case TASK_KIND_THEN:
            if (t->snd == NULL) {
                Result r = poll(t->fst);
                switch (r.state) {
                    case STATE_DONE:
                        t->snd = t->func(r);
                        break;
                    case STATE_PENDING:
                        break;
                }
                return RESULT_PENDING;
            }
            return poll(t->snd);
        case TASK_KIND_WAIT:
            if (!t->started) {
                t->start = time(NULL);
                t->started = true;
                return RESULT_PENDING;
            }
            time_t now = time(NULL);
            if (difftime(now, t->start) >= t->duration) return RESULT_DONE;
            return RESULT_PENDING;
        case TASK_KIND_LOG:
            printf("[LOG] %s\n", t->message);
            return RESULT_DONE;
        case TASK_KIND_CONTEXT_FIFO:
            if (t->input_fd < 0) {
                t->input_fd = make_and_open_fifo();
                printf("[INFO] opened fifo successfully\n");

                Result ret = RESULT_DONE;
                ret.kind = RESULT_KIND_FILE_DESCRIPTOR;
                ret.fd = t->input_fd;
                t->body = t->build_body(ret);
                return RESULT_PENDING;
            }
            assert(t->body != NULL);
            Result ret = poll(t->body);
            switch (ret.state) {
                case STATE_DONE:
                    close_and_unlink_fifo(t->input_fd);
                    printf("[INFO] closed fifo successfully\n");
                    break;
                case STATE_PENDING:
                    break;
            }
            return ret;
        case TASK_KIND_READ_FIFO:
            ssize_t r = read(t->input_fd, read_buf, READ_BUF_CAPACITY-1);
            if (r == 0) {
                return RESULT_PENDING;
            } else if (r == -1 && errno == EAGAIN) {
                return RESULT_PENDING;
            } else if (r > 0) {
                assert(r < READ_BUF_CAPACITY-1);
                read_buf[r] = '\0';
                Result ret = RESULT_DONE;
                ret.kind = RESULT_KIND_COMMAND;
                ret.command = read_buf;
                return ret;
            } else {
                printf("[ERROR] Could not read from file: %s\n", strerror(errno));
                exit(1);
            }
        case TASK_KIND_COUNTER:
            set_color(t->color);
            if (t->cur < t->end) {
                printf("Counter %s: %d\n", t->name, t->cur);
                t->cur++;
                printf("\x1b[0m");
                fflush(stdout);
                return RESULT_PENDING;
            }
            printf("Counter %s: Finished\n", t->name);
            reset_color();
            fflush(stdout);
            return RESULT_DONE;
    }
    UNREACHABLE("poll");
}

Task *build_log(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_COMMAND);
    Task *log = task_alloc();
    log->kind = TASK_KIND_LOG;
    log->message = r.command;
    return log;
}

Task *build_readlog(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_FILE_DESCRIPTOR);

    Task *read = task_alloc();
    read->kind = TASK_KIND_READ_FIFO;
    read->input_fd = r.fd;

    Task *glue = task_alloc();
    glue->kind = TASK_KIND_THEN;
    glue->fst = read;
    glue->snd = NULL;
    glue->func = build_log;

    return glue;
}

int main() {
    task_free_all();

    Task read1 = {
        .kind = TASK_KIND_READ_FIFO,
        .input_fd = -1,
    };

    Task readlog1 = {
        .kind = TASK_KIND_THEN,
        .fst = &read1,
        .snd = NULL,
        .func = build_log,
    };

    Task read2 = {
        .kind = TASK_KIND_READ_FIFO,
        .input_fd = -1,
    };
    UNUSED(readlog1);

    Task readlog2 = {
        .kind = TASK_KIND_THEN,
        .fst = &read2,
        .snd = NULL,
        .func = build_log,
    };
    UNUSED(readlog2);

    Task foo = {
        .kind = TASK_KIND_COUNTER,
        .cur = 0,
        .end = 10,
        .color = RED,
    };
    UNUSED(foo);

    Task fifo = {
        .kind = TASK_KIND_CONTEXT_FIFO,
        .input_fd = -1,
        .body = NULL,
        .build_body = build_readlog,
    };

    printf("[INFO] starting server\n");
    Result r = poll(&fifo);
    while (r.state != STATE_DONE) {
        r = poll(&fifo);
    }
    printf("[INFO] finishing server\n");
}
