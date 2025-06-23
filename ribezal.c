#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "devutils.h"

// thirdparty
#include <curl/curl.h>

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

#define STRING_BUILDER_INITIAL_CAPACITY 16
#define STRING_BUILDER_MAXIMUM_CAPACITY 1024

typedef struct {
    char *str;
    size_t capacity;
    size_t count;
} String_Builder;

String_Builder string_builder_new() {
    String_Builder sb;
    sb.count = 0;
    sb.capacity = STRING_BUILDER_INITIAL_CAPACITY;
    sb.str = malloc(sizeof(char) * sb.capacity);
    return sb;
}

void string_builder_task_destroy(String_Builder sb) {
    free(sb.str);
    sb.str = NULL;
}

void string_builder_clear(String_Builder *sb) {
    sb->count = 0;
}

void string_builder_grow(String_Builder *sb) {
    size_t new_capacity = 2*sb->capacity;
    assert(new_capacity <= STRING_BUILDER_MAXIMUM_CAPACITY);
    sb->str = realloc(sb->str, new_capacity * sizeof(char));
    if (sb->str == NULL) {
        UNIMPLEMENTED("string_builder_grow");
    }
    sb->capacity = new_capacity;
}

void string_builder_append(String_Builder *sb, char c) {
    while (sb->count >= sb->capacity) string_builder_grow(sb);
    sb->str[sb->count] = c;
    sb->count++;
    return;
}

void string_builder_append_str(String_Builder *sb, char *str) {
    while (*str != '\0') {
        string_builder_append(sb, *str);
        str++;
    }
}

CHECK_PRINTF_FMT(2, 3) int string_builder_printf(String_Builder *sb, char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    assert(n >= 0);

    if ((size_t) n+1 > sb->capacity) {
        UNIMPLEMENTED("string_builder_printf");
    }

    va_start(args, format);
    n = vsnprintf(sb->str, n+1, format, args);
    va_end(args);

    assert(n >= 0);
    sb->count = n;
    return n;
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
    RESULT_KIND_BOOL,
    RESULT_KIND_INT,
    RESULT_KIND_INT_TUPLE,
} Result_Kind;

typedef struct {
    State state;
    Result_Kind kind;
    // possible values
    bool bool_val;
    int x;
    int y;

    // possible contexts
    bool has_fd;
    int fd;
    bool has_string_builder;
    String_Builder *string_builder;
} Result;

#define RESULT_DONE    (Result) {.state = STATE_DONE,    .kind = RESULT_KIND_VOID, .has_string_builder = false, .has_fd = false}
#define RESULT_PENDING (Result) {.state = STATE_PENDING, .kind = RESULT_KIND_VOID, .has_string_builder = false, .has_fd = false}

Result result_int(int x) {
    Result r = RESULT_DONE;
    r.kind = RESULT_KIND_INT;
    r.x = x;
    return r;
}

Result result_int_tuple(int x, int y) {
    Result r = RESULT_DONE;
    r.kind = RESULT_KIND_INT_TUPLE;
    r.x = x;
    r.y = y;
    return r;
}

typedef struct {
    bool in_global_curl;
    bool in_multi_curl;
    bool in_easy_curl;
    CURLM *multi_handle;
    CURL *easy_handle;
} Context;

Context context_new() {
    Context c = {
        .in_global_curl = false,
        .in_multi_curl = false,
        .in_easy_curl = false,
        .multi_handle = NULL,
        .easy_handle = NULL,
    };
    return c;
}

typedef enum {
    TASK_KIND_CONST,
    TASK_KIND_SEQUENCE,
    TASK_KIND_PARALLEL,
    TASK_KIND_THEN,
    TASK_KIND_ITERATE,
    TASK_KIND_LESS_THAN,
    TASK_KIND_WAIT,
    TASK_KIND_LOG,
    TASK_KIND_FIFO_CONTEXT,
    TASK_KIND_FIFO_REPL,
    TASK_KIND_STRING_BUILDER_CONTEXT,
    TASK_KIND_CURL_GLOBAL_CONTEXT,
    TASK_KIND_CURL_MULTI_CONTEXT,
    TASK_KIND_CURL_EASY_CONTEXT,
    TASK_KIND_CURL_PERFORM,
} Task_Kind;

typedef struct Task Task;
typedef Task *(*Then_Function)(Result);
typedef bool (*Predicate)(Result);

#define MAX_SEQ_COUNT 4
#define MAX_PAR_COUNT 4

struct Task {
    Task_Kind kind;
    union {
        // TASK_KIND_CONST
        struct {
            Result const_result;
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
            Task *par[MAX_PAR_COUNT];
        };
        // TASK_KIND_THEN
        struct {
            Task *fst;
            Task *snd;
            Then_Function then;
        };
        // TASK_KIND_ITERATE
        struct {
            int iter_phase;
            Task *iter_body;
            Task *iter_condition;
            Result last;
            Then_Function iter_next;
            Then_Function iter_build_condition;
        };
        // TASK_KIND_LOG
        struct {
            String_Builder *log_msg;
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
        // TASK_KIND_STRING_BUILDER_CONTEXT
        struct {
            Task *sb_head;
            Task *sb_body;
            Then_Function sb_build;
            String_Builder sb;
        };
        // TASK_KIND_CURL_GLOBAL_CONTEXT
        struct {
            bool curl_global_is_init;
            Task *curl_global_body;
        };
        // TASK_KIND_CURL_MULTI_CONTEXT
        struct {
            bool curl_multi_is_init;
            Task *curl_multi_body;
        };
        // TASK_KIND_CURL_EASY_CONTEXT
        struct {
            bool curl_easy_is_init;
            Task *curl_easy_body;
        };
    };
};

#define TASK_POOL_CAPACITY 16
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

void task_destroy(Task *t) {
    switch (t->kind) {
        case TASK_KIND_CONST:
            break;
        case TASK_KIND_SEQUENCE:
            break;
        case TASK_KIND_PARALLEL:
        case TASK_KIND_THEN:
            UNIMPLEMENTED("task_destroy");
        case TASK_KIND_ITERATE:
            break;
        case TASK_KIND_LESS_THAN:
            UNIMPLEMENTED("task_destroy");
        case TASK_KIND_WAIT:
            break;
        case TASK_KIND_LOG:
            break;
        case TASK_KIND_FIFO_CONTEXT:
            break;
        case TASK_KIND_FIFO_REPL:
            break;
        case TASK_KIND_STRING_BUILDER_CONTEXT:
            break;
        case TASK_KIND_CURL_GLOBAL_CONTEXT:
            break;
        case TASK_KIND_CURL_MULTI_CONTEXT:
            break;
        case TASK_KIND_CURL_EASY_CONTEXT:
            break;
        case TASK_KIND_CURL_PERFORM:
            break;
    }
    if (task_in_pool(t)) task_free(t);
}

Result task_poll(Task *t, Context *ctx) {
    assert(t != NULL);
    switch (t->kind) {
        case TASK_KIND_CONST:
            return t->const_result;
        case TASK_KIND_SEQUENCE: 
            {
                if (t->seq_count == 0) return RESULT_DONE;
                assert(t->seq_index < t->seq_count);
                Result r = task_poll(t->seq[t->seq_index], ctx);
                switch (r.state) {
                    case STATE_DONE:
                        task_destroy(t->seq[t->seq_index]);
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
                Result r = task_poll(t->par[t->par_index], ctx);
                switch (r.state) {
                    case STATE_DONE:
                        task_destroy(t->par[t->par_index]);
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
                Result r = task_poll(t->fst, ctx);
                switch (r.state) {
                    case STATE_DONE:
                        t->snd = t->then(r);
                        break;
                    case STATE_PENDING:
                        break;
                }
                return RESULT_PENDING;
            }
            return task_poll(t->snd, ctx);
        case TASK_KIND_ITERATE:
            switch (t->iter_phase) {
                case 0:
                    assert(t->iter_body != NULL);
                    t->last = task_poll(t->iter_body, ctx);
                    switch (t->last.state) {
                        case STATE_DONE:
                            task_destroy(t->iter_body);
                            t->iter_body = NULL;
                            t->iter_phase = 1;
                            t->iter_condition = t->iter_build_condition(t->last);
                            break;
                        case STATE_PENDING:
                            break;
                    }
                    return RESULT_PENDING;
                case 1:
                    assert(t->iter_condition != NULL);
                    Result r = task_poll(t->iter_condition, ctx);
                    switch (r.state) {
                        case STATE_DONE:
                            task_destroy(t->iter_condition);
                            t->iter_condition = NULL;
                            assert(r.kind == RESULT_KIND_BOOL);
                            if (r.bool_val) {
                                t->iter_phase = 0;
                                t->iter_body = t->iter_next(t->last);
                                return RESULT_PENDING;
                            } else {
                                assert(t->last.state == STATE_DONE);
                                return t->last;
                            }
                        case STATE_PENDING:
                            UNIMPLEMENTED("task_poll");
                    }
                    UNIMPLEMENTED("task_poll");
            }
            UNREACHABLE("invalid phase");
        case TASK_KIND_LESS_THAN:
            UNIMPLEMENTED("task_poll");
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
            string_builder_append(t->log_msg, '\0');
            printf("[LOG] %s\n", t->log_msg->str);
            return RESULT_DONE;
        case TASK_KIND_FIFO_CONTEXT:
            if (t->fifo_fd < 0) {
                t->fifo_fd = make_and_open_fifo();
                printf("[INFO] opened fifo successfully\n");

                Result ret = RESULT_DONE;
                ret.has_fd = true;
                ret.fd = t->fifo_fd;
                t->body = t->build_body(ret);
                return RESULT_PENDING;
            }
            assert(t->body != NULL);
            Result ret = task_poll(t->body, ctx);
            switch (ret.state) {
                case STATE_DONE:
                    task_destroy(t->body);
                    close_and_unlink_fifo(t->fifo_fd);
                    printf("[INFO] closed fifo successfully\n");
                    break;
                case STATE_PENDING:
                    break;
            }
            return ret;
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
        case TASK_KIND_STRING_BUILDER_CONTEXT:
            {
                if (t->sb_body == NULL) {
                    assert(t->sb_head != NULL);
                    Result r = task_poll(t->sb_head, ctx);
                    switch (r.state) {
                        case STATE_DONE:
                            task_destroy(t->sb_head);
                            t->sb = string_builder_new();

                            r.has_string_builder = true;
                            r.string_builder = &t->sb;

                            t->sb_body = t->sb_build(r);
                            assert(t->sb_body != NULL);
                            return RESULT_PENDING;
                        case STATE_PENDING:
                            return r;
                    }
                }
                Result r = task_poll(t->sb_body, ctx);
                switch (r.state) {
                    case STATE_DONE:
                        task_destroy(t->sb_body);
                        string_builder_task_destroy(t->sb);
                        break;
                    case STATE_PENDING:
                }
                return r;
            }
        case TASK_KIND_CURL_GLOBAL_CONTEXT:
            {
                if (!t->curl_global_is_init) {
                    CURLcode r = curl_global_init(CURL_GLOBAL_DEFAULT);
                    if (r != 0) {
                        UNIMPLEMENTED("task_poll");
                    }
                    t->curl_global_is_init = true;
                    ctx->in_global_curl = true;
                }
                Result r = task_poll(t->curl_global_body, ctx);
                switch (r.state) {
                    case STATE_DONE:
                        task_destroy(t->curl_global_body);
                        curl_global_cleanup();
                        ctx->in_global_curl = false;
                        break;
                    case STATE_PENDING:
                        break;
                }
                return r;
            }
        case TASK_KIND_CURL_MULTI_CONTEXT:
            {
                assert(ctx->in_global_curl);
                if (!t->curl_multi_is_init) {
                    ctx->multi_handle = curl_multi_init();
                    if (ctx->multi_handle == NULL) {
                        UNIMPLEMENTED("task_poll");
                    }
                    t->curl_multi_is_init = true;
                    ctx->in_multi_curl = true;
                }
                assert(ctx->multi_handle != NULL);
                Result r = task_poll(t->curl_multi_body, ctx);
                switch (r.state) {
                    case STATE_DONE:
                        task_destroy(t->curl_multi_body);
                        CURLMcode code = curl_multi_cleanup(ctx->multi_handle);
                        if (code != CURLM_OK) {
                            UNIMPLEMENTED("task_poll");
                        }
                        ctx->in_multi_curl = false;
                        break;
                    case STATE_PENDING:
                        UNIMPLEMENTED("task_poll");
                }
                return r;
            }
        case TASK_KIND_CURL_EASY_CONTEXT:
            assert(ctx->in_global_curl);
            if (ctx->in_multi_curl) {
                UNIMPLEMENTED("task_poll");
            } else {
                if (!t->curl_easy_is_init) {
                    ctx->easy_handle = curl_easy_init();
                    if (ctx->easy_handle == NULL) {
                        UNIMPLEMENTED("task_poll");
                    }
                    CURLcode code = curl_easy_setopt(ctx->easy_handle, CURLOPT_URL, "https://example.com");
                    if (code != CURLE_OK) {
                        UNIMPLEMENTED("task_poll");
                    }
                    
                    t->curl_easy_is_init = true;
                    ctx->in_easy_curl = true;
                }
                assert(t->curl_easy_body != NULL);
                Result r = task_poll(t->curl_easy_body, ctx);
                switch (r.state) {
                    case STATE_DONE:
                        task_destroy(t->curl_easy_body);
                        curl_easy_cleanup(ctx->easy_handle);
                        ctx->in_easy_curl = false;
                        break;
                    case STATE_PENDING:
                        break;
                }
                return r;
            }
        case TASK_KIND_CURL_PERFORM:
            assert(ctx->in_easy_curl);
            printf("[INFO] performing request\n");
            CURLcode code;
            code = curl_easy_perform(ctx->easy_handle);
            printf("[INFO] performed request\n");
            if (code != CURLE_OK) {
                UNIMPLEMENTED("task_poll");
            }
            return RESULT_DONE;
    }
    UNREACHABLE("task_poll");
}

Task *task_const(Result r) {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_CONST;
    ret->const_result = r;
    return ret;
}

Task *task_log(String_Builder *msg) {
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

Task *task_sequence() {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_SEQUENCE;
    ret->seq_count = 0;
    ret->seq_index = 0;
    return ret;
}

void task_seq_append(Task *s, Task *t) {
    assert(s->kind == TASK_KIND_SEQUENCE);
    assert(s->seq_count < MAX_SEQ_COUNT); 

    s->seq[s->seq_count] = t;
    s->seq_count++;
}

Task *task_parallel() {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_PARALLEL;
    ret->par_count = 0;
    ret->par_index = 0;
    return ret;
}

void task_par_append(Task *p, Task *t) {
    assert(p->kind == TASK_KIND_PARALLEL);
    assert(p->par_count < MAX_PAR_COUNT);

    p->par[p->par_count] = t;
    p->par_count++;
}

Task *task_iterate(Task *start, Then_Function next, Then_Function cond) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_ITERATE;
    t->iter_phase = 0;
    t->iter_body = start;
    t->iter_next = next;
    t->iter_build_condition = cond;
    return t;
}

Task *repl(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.has_fd);

    Task *repl = task_alloc();
    repl->kind = TASK_KIND_FIFO_REPL;
    repl->file_desc = r.fd;

    return repl;
}

Task *less_than(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_INT_TUPLE);

    int x = r.x;
    int y = r.y;

    r.kind = RESULT_KIND_BOOL;
    r.bool_val = x < y;
    return task_const(r);
}

Task *counter_inc(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_INT_TUPLE);
    assert(r.has_string_builder);

    Task *ret = task_sequence();

    string_builder_clear(r.string_builder);
    string_builder_printf(r.string_builder, "Counter: %d", r.x);
    task_seq_append(ret, task_log(r.string_builder));

    task_seq_append(ret, task_wait(1.0));

    r.x++;
    task_seq_append(ret, task_const(r));

    return ret;
}

Task *counter(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.has_string_builder);
    assert(r.kind == RESULT_KIND_INT_TUPLE);

    return task_iterate(task_const(r), counter_inc, less_than);
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

    Task foo = {
        .kind = TASK_KIND_STRING_BUILDER_CONTEXT,
        .sb_head = task_const(result_int_tuple(0, 5)),
        .sb_body = NULL,
        .sb_build = counter,
    };

    Task bar = {
        .kind = TASK_KIND_STRING_BUILDER_CONTEXT,
        .sb_head = task_const(result_int_tuple(20, 30)),
        .sb_body = NULL,
        .sb_build = counter,
    };
    
    String_Builder msg = string_builder_new();
    string_builder_append_str(&msg, "inside curl easy");

    Task request = {
        .kind = TASK_KIND_CURL_PERFORM,
    };

    Task curl_easy = {
        .kind = TASK_KIND_CURL_EASY_CONTEXT,
        .curl_easy_is_init = false,
        .curl_easy_body = task_sequence(),
    };

    task_seq_append(curl_easy.curl_easy_body, task_log(&msg));
    task_seq_append(curl_easy.curl_easy_body, &request);

    Task curl_global = {
        .kind = TASK_KIND_CURL_GLOBAL_CONTEXT,
        .curl_global_is_init = false,
        .curl_global_body = &curl_easy,
    };

    Task *baz = task_parallel();
    UNUSED(foo);
    UNUSED(bar);
    UNUSED(fifo);
    task_par_append(baz, &curl_global);

    Context ctx = context_new();

    printf("[INFO] starting server\n");
    Result r = task_poll(baz, &ctx);
    while (r.state != STATE_DONE) {
        r = task_poll(baz, &ctx);
    }
    printf("[INFO] finishing server\n");
    
    size_t count = 0;
    for (Task_Free_Node *i = task_pool_head; i != NULL; i = i->next) count++;
    printf("[INFO] memory leaked %zu tasks from the pool\n", TASK_POOL_CAPACITY - count);

    printf("[INFO] Stack: ");
    stack_print();
}
