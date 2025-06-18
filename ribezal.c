#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

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

typedef enum {
    STACK_VALUE_STRING,
    STACK_VALUE_INT,
} Stack_Value_Kind;

typedef struct {
    Stack_Value_Kind kind;
    union {
        int x;
        char *str;
    };
} Stack_Value;

#define MAX_STACK_SIZE 8
Stack_Value stack[MAX_STACK_SIZE];
size_t stack_count = 0;

void stack_push_string(char *str) {
    size_t l = strlen(str);
    char *dest = malloc((l+1)*sizeof(char));
    strcpy(dest, str);
    assert(dest[l] == '\0');

    assert(stack_count < MAX_STACK_SIZE);
    stack[stack_count].kind = STACK_VALUE_STRING;
    stack[stack_count].str  = dest;
    stack_count++;
}

void stack_push_int(int x) {
    assert(stack_count < MAX_STACK_SIZE);
    stack[stack_count].kind = STACK_VALUE_INT;
    stack[stack_count].x = x;
    stack_count++;
}

void stack_drop() {
    if (stack_count == 0) return;
    switch (stack[stack_count-1].kind) {
        case STACK_VALUE_STRING:
            free(stack[stack_count-1].str);
            break;
        case STACK_VALUE_INT:
            break;
    }
    stack_count--;
}

bool stack_two_int() {
    if (stack_count < 2) return false;
    size_t i1 = stack_count-1;
    size_t i2 = stack_count-2;
    return (stack[i1].kind == STACK_VALUE_INT) && (stack[i2].kind == STACK_VALUE_INT);
}

void stack_print() {
    printf("[");
    for (size_t i=0; i+1<stack_count; i++) {
        switch (stack[i].kind) {
            case STACK_VALUE_STRING: printf("%s, ", stack[i].str); break;
            case STACK_VALUE_INT:    printf("%d, ", stack[i].x); break;
        }
    }
    if (stack_count > 0) {
        size_t i = stack_count-1;
        switch (stack[i].kind) {
            case STACK_VALUE_STRING: printf("%s", stack[i].str); break;
            case STACK_VALUE_INT:    printf("%d", stack[i].x); break;
        }
    }
    printf("]\n");
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
    RESULT_KIND_INT,
    RESULT_KIND_COMMAND,
    RESULT_KIND_FILE_DESCRIPTOR,
} Result_Kind;

typedef struct {
    State state;
    Result_Kind kind;
    int x;
    char *command;
    int fd;
} Result;

#define RESULT_DONE    (Result) {.state = STATE_DONE, .kind = RESULT_KIND_VOID}
#define RESULT_PENDING (Result) {.state = STATE_PENDING, .kind = RESULT_KIND_VOID}

typedef enum {
    TASK_KIND_CONST,
    TASK_KIND_SEQUENCE,
    TASK_KIND_PARALLEL,
    TASK_KIND_THEN,
    TASK_KIND_ITERATE,
    TASK_KIND_WAIT,
    TASK_KIND_LOG,
    TASK_KIND_FIFO_CONTEXT,
    TASK_KIND_FIFO_READ,
    TASK_KIND_FIFO_REPL,
} Task_Kind;

typedef struct Task Task;
typedef Task *(*Then_Function)(Result);
typedef bool (*Predicate)(Result);

#define MAX_SEQ_COUNT 4

struct Task {
    Task_Kind kind;
    union {
        // TASK_KIND_CONST
        struct {
            Result const_result;
        };
        // TASK_KIND_COUNTER
        struct {
            int counter_cur;
            int counter_end;
            char *counter_name;
            Color color;
        };
        // TASK_KIND_SEQUENCE
        struct {
            size_t seq_count;
            size_t seq_index;
            Task *seq[MAX_SEQ_COUNT];
        };
        // TASK_KIND_PARALLEL
        struct {
            size_t par_count;
            size_t par_index;
            Task **par;
        };
        // TASK_KIND_THEN
        struct {
            Task *fst;
            Task *snd;
            Then_Function then;
        };
        // TASK_KIND_ITERATE
        struct {
            Task *iter_body;
            Then_Function iter_next;
            Predicate iter_condition;
        };
        // TASK_KIND_LOG
        struct {
            char *log_msg;
        };
        // TASK_KIND_WAIT
        struct {
            double duration;
            bool started;
            time_t start;
        };
        // TASK_KIND_FIFO_READ, TASK_KIND_FIFO_REPL
        struct {
            int file_desc;
        };
        // TASK_KIND_FIFO_CONTEXT
        struct {
            Task *body;
            Then_Function build_body;
            int fifo_fd;
        };
    };
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

bool task_in_pool(Task *t) {
    return task_pool <= t && t < task_pool + TASK_POOL_CAPACITY;
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
    if (t == NULL) return;

    //make sure t is actually in the task pool and does not come from somewhere else
    assert(task_in_pool(t));

    Task_Free_Node *tfree = (Task_Free_Node *) t;
    tfree->next = task_pool_head;
    task_pool_head = tfree;
}

typedef enum {
    REPLY_CLOSE,
    REPLY_ACK,
    REPLY_REJECT,
} Reply_Kind;

#define SPACES " \f\n\r\t\v"

Reply_Kind execute(char *prog) {
    for (char *token = strtok(prog, SPACES); token != NULL; token = strtok(NULL, SPACES)) {
        char *endptr;
        int val = strtol(token, &endptr, 10);
        if (*endptr == '\0') {
            stack_push_int(val);
        } else if (strcmp(token, "quit") == 0) {
            return REPLY_CLOSE;
        } else if (strcmp(token, "print") == 0) {
            stack_print();
        } else if (strcmp(token, "drop") == 0) {
            stack_drop();
        } else if (strcmp(token, "clear") == 0) {
            while (stack_count > 0) stack_drop();
        } else if (strcmp(token, "+") == 0) {
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x += x;
            }
        } else if (strcmp(token, "-") == 0) {
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x -= x;
            }
        } else if (strcmp(token, "*") == 0) {
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x *= x;
            }
        } else if (strcmp(token, "/") == 0) {
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x /= x;
            }
        } else {
            bool all_graph = true;
            for (char *c = token; *c != '\0'; c++) {
                all_graph = all_graph && isgraph(*c);
            }
            if (all_graph) {
                stack_push_string(token);
            } else {
                return REPLY_REJECT;
            }
        }
    }
    return REPLY_ACK;
}

// TODO: multiple read tasks can use this so every read task should have its own
#define READ_BUF_CAPACITY 32
char read_buf[READ_BUF_CAPACITY];

// TODO: multiple log tasks can use this so every log task should have its own
#define LOG_BUF_CAPACITY 32
char log_buf[LOG_BUF_CAPACITY];

void destroy(Task *t) {
    switch (t->kind) {
        case TASK_KIND_CONST:
            break;
        case TASK_KIND_SEQUENCE:
            break;
        case TASK_KIND_PARALLEL:
        case TASK_KIND_THEN:
        case TASK_KIND_ITERATE:
            UNIMPLEMENTED("destroy");
        case TASK_KIND_WAIT:
            break;
        case TASK_KIND_LOG:
            log_buf[0] = '\0';
            break;
        case TASK_KIND_FIFO_CONTEXT:
        case TASK_KIND_FIFO_READ:
        case TASK_KIND_FIFO_REPL:
            UNIMPLEMENTED("destroy");
    }
    if (task_in_pool(t)) task_free(t);
}

Result poll(Task *t) {
    switch (t->kind) {
        case TASK_KIND_CONST:
            return t->const_result;
        case TASK_KIND_SEQUENCE: 
            {
                assert(t->seq_index < t->seq_count);
                Result r = poll(t->seq[t->seq_index]);
                switch (r.state) {
                    case STATE_DONE:
                        destroy(t->seq[t->seq_index]);
                        t->seq_index++;

                        // if this was the last Task in the sequence we should return its result
                        if (t->seq_index == t->seq_count) return r;
                        break;
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
                        t->snd = t->then(r);
                        break;
                    case STATE_PENDING:
                        break;
                }
                return RESULT_PENDING;
            }
            return poll(t->snd);
        case TASK_KIND_ITERATE:
            assert(t->iter_body != NULL);
            Result r = poll(t->iter_body);
            switch (r.state) {
                case STATE_DONE:
                    destroy(t->iter_body);
                    if (t->iter_condition(r)) {
                        t->iter_body = t->iter_next(r);
                        return RESULT_PENDING;
                    } else {
                        return r;
                    }
                case STATE_PENDING:
                    return r;
            }
            UNREACHABLE("invalid Result_Kind");
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
            printf("[LOG] %s\n", t->log_msg);
            return RESULT_DONE;
        case TASK_KIND_FIFO_CONTEXT:
            if (t->fifo_fd < 0) {
                t->fifo_fd = make_and_open_fifo();
                printf("[INFO] opened fifo successfully\n");

                Result ret = RESULT_DONE;
                ret.kind = RESULT_KIND_FILE_DESCRIPTOR;
                ret.fd = t->fifo_fd;
                t->body = t->build_body(ret);
                return RESULT_PENDING;
            }
            assert(t->body != NULL);
            Result ret = poll(t->body);
            switch (ret.state) {
                case STATE_DONE:
                    close_and_unlink_fifo(t->fifo_fd);
                    printf("[INFO] closed fifo successfully\n");
                    break;
                case STATE_PENDING:
                    break;
            }
            return ret;
        case TASK_KIND_FIFO_READ:
            {
                ssize_t r = read(t->file_desc, read_buf, READ_BUF_CAPACITY-1);
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
            }
        case TASK_KIND_FIFO_REPL:
            {
                ssize_t r = read(t->file_desc, read_buf, READ_BUF_CAPACITY-1);
                if (r == 0) {
                    return RESULT_PENDING;
                } else if (r == -1 && errno == EAGAIN) {
                    return RESULT_PENDING;
                } else if (r > 0) {
                    assert(r < READ_BUF_CAPACITY);
                    read_buf[r] = '\0';
                    switch (execute(read_buf)) {
                        case REPLY_CLOSE:
                            return RESULT_DONE;
                        case REPLY_REJECT:
                            printf("Unknown command\n");
                            return RESULT_PENDING;
                        case REPLY_ACK:
                            return RESULT_PENDING;
                    }
                    UNREACHABLE("invalid Result_Kind");
                } else {
                    printf("[ERROR] Could not read from file: %s\n", strerror(errno));
                    exit(1);
                }
            }
    }
    UNREACHABLE("poll");
}

Task *task_const(Result r) {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_CONST;
    ret->const_result = r;
    return ret;
}

Task *task_log(char *msg) {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_LOG;
    ret->log_msg = msg;
    return ret;
}

Task *task_wait(double dur) {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_WAIT;
    ret->started = false;
    ret->duration = dur;
    ret->start = 0;
    return ret;
}

Task *repl(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_FILE_DESCRIPTOR);

    Task *repl = task_alloc();
    repl->kind = TASK_KIND_FIFO_REPL;
    repl->file_desc = r.fd;

    return repl;
}

Task *next(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_INT);

    Task *ret = task_alloc();
    ret->kind = TASK_KIND_SEQUENCE;
    ret->seq_count = 3;
    ret->seq_index = 0;

    int l = snprintf(log_buf, LOG_BUF_CAPACITY, "Counter: %d", r.x);
    assert(log_buf[l] == '\0');
    ret->seq[0] = task_log(log_buf);

    ret->seq[1] = task_wait(1.0);

    r.x++;
    ret->seq[2] = task_const(r);

    return ret;
}

bool condition(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_INT);

    return r.x < 10;
}

int main() {
    // We need to do this to initialize the pool allocator
    task_free_all();

    Task fifo = {
        .kind = TASK_KIND_FIFO_CONTEXT,
        .fifo_fd = -1,
        .body = NULL,
        .build_body = repl,
    };
    UNUSED(fifo);

    Result result_zero = {
        .state = STATE_DONE,
        .kind = RESULT_KIND_INT,
        .x = 0,
    };
    Task *task_zero = task_const(result_zero);

    Task foo = {
        .kind = TASK_KIND_ITERATE,
        .iter_body = task_zero,
        .iter_next = next,
        .iter_condition = condition,
    };

    printf("[INFO] starting server\n");
    Result r = poll(&foo);
    while (r.state != STATE_DONE) {
        r = poll(&foo);
    }
    printf("[INFO] finishing server\n");
    
    size_t count = 0;
    for (Task_Free_Node *i = task_pool_head; i != NULL; i = i->next) count++;
    printf("[INFO] memory leaked %zu tasks from the pool\n", TASK_POOL_CAPACITY - count);

    printf("[INFO] Stack: ");
    stack_print();
}
