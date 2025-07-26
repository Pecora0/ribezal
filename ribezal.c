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
#include "tgapi.h"
#include "command.h"

// thirdparty
#include <curl/curl.h>
#include "thirdparty/json.h"
#define ARENA_IMPLEMENTATION
#include "thirdparty/arena.h"

#define STRING_BUILDER_INITIAL_CAPACITY 16
#define STRING_BUILDER_MAXIMUM_CAPACITY 2048

/************************
 * type definitions     *
 ************************/

// I have the pointer to the Arena in there so that I can pass it conveniently to callback functions (namely curl_write_cb)
// It is "standalone" but it may be confusing. Is there another possibility?
typedef struct {
    Arena *arena;
    char *items;
    size_t capacity;
    size_t count;
} Arena_String_Builder;

typedef struct {
    const char *str;
    size_t count;
} String_View;

typedef enum {
    STACK_VALUE_STRING,
    STACK_VALUE_INT,
} Stack_Value_Kind;

typedef struct {
    Stack_Value_Kind kind;
    union {
        int x;
        struct {
            char *str;
            size_t count;
        };
    };
} Stack_Value;

#define MAX_STACK_SIZE 8
Stack_Value stack[MAX_STACK_SIZE];
size_t stack_count = 0;

#define STACK_TOP (stack[stack_count-1])

typedef enum {
    STATE_DONE,
    STATE_PENDING,
    STATE_ERROR,
} State;

typedef enum {
    RESULT_KIND_VOID,
    RESULT_KIND_BOOL,
    RESULT_KIND_INT,
    RESULT_KIND_STRING_VIEW,
    RESULT_KIND_JSON_VALUE,
} Result_Kind;

typedef struct {
    State state;
    Result_Kind kind;
    // possible values
    bool bool_val;
    int x;
    String_View string_view;
    json_value_t *json_value;
} Result;

typedef enum {
    CONTEXT_KIND_FIFO,
    CONTEXT_KIND_ARENA,
    CONTEXT_KIND_CURL_GLOBAL,
    CONTEXT_KIND_CURL_MULTI,
    CONTEXT_KIND_CURL_EASY,
    CONTEXT_KIND_COUNT,
} Context_Kind;

typedef struct {
    bool flag[CONTEXT_KIND_COUNT];
    Arena *arena;
    CURLM *multi_handle;
    CURL *easy_handle;
    int file_descriptor;
} Context;

typedef enum {
    TASK_KIND_CONST,
    TASK_KIND_SEQUENCE,
    TASK_KIND_PARALLEL,
    TASK_KIND_THEN,
    TASK_KIND_ITERATE,
    TASK_KIND_WAIT,
    TASK_KIND_LOG,
    TASK_KIND_FIFO_REPL,
    TASK_KIND_CONTEXT,
    TASK_KIND_CURL_PERFORM,
    TASK_KIND_PARSE_JSON_VALUE,
    TASK_KIND_GET_TG_USER,
    TASK_KIND_GET_TG_UPDATE_LIST,
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
            Context sub_ctx[MAX_PAR_COUNT];
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
            String_View log_msg;
        };
        // TASK_KIND_WAIT
        struct {
            double duration;
            bool started;
            time_t start;
        };
        // TASK_KIND_CONTEXT
        struct {
            Context_Kind context_kind;
            Task *context_body;
            // Only used if context_kind == CONTEXT_KIND_ARENA
            Arena context_arena;
        };
        // TASK_KIND_CURL_PERFORM
        struct {
            String_View url;
            Arena_String_Builder curl_perform_sb;
        };
        // TASK_KIND_PARSE_JSON_VALUE
        struct {
            String_View json_source_str;
        };
        // TASK_KIND_GET_TG_*
        struct {
            json_value_t *json_root;
        };
    };
};

#define TASK_POOL_CAPACITY 24
Task task_pool[TASK_POOL_CAPACITY];
typedef struct Task_Free_Node Task_Free_Node;
struct Task_Free_Node {
    Task_Free_Node *next;
};
static_assert(sizeof(Task_Free_Node) <= sizeof(Task));
Task_Free_Node *task_pool_head = NULL;

typedef enum {
    REPLY_CLOSE,
    REPLY_ACK,
    REPLY_ERROR,
} Reply_Kind;

/******************************
 * functions                  *
 ******************************/

/******************************
 * string_view_*              *
 ******************************/

String_View string_view_from_arena_string_builder(Arena_String_Builder sb) {
    String_View result = {
        .str = sb.items,
        .count = sb.count,
    };
    return result;
}

String_View string_view_from_char_ptr(char *ptr) {
    String_View result = {
        .str = ptr,
        .count = strlen(ptr),
    };
    return result;
}

String_View string_view_drop_ws(String_View sv) {
    while (sv.count > 0 && isspace(sv.str[0])) {
        sv.str++;
        sv.count--;
    }
    return sv;
}

String_View string_view_drop_non_ws(String_View sv) {
    while (sv.count > 0 && !isspace(sv.str[0])) {
        sv.str++;
        sv.count--;
    }
    return sv;
}

String_View string_view_take_non_ws(String_View sv) {
    size_t i=0;
    while (i < sv.count && !isspace(sv.str[i])) i++;
    String_View result = {
        .str = sv.str,
        .count = i,
    };
    return result;
}

bool string_view_try_parse_int(String_View sv, int *result) {
    if (sv.count == 0) return false;
    int acc = 0;
    for (size_t i=0; i<sv.count; i++) {
        char c = sv.str[i];
        if ('0' <= c && c <= '9') {
            acc = 10*acc + (c - '0');
        } else {
            return false;
        }
    }
    *result = acc;
    return true;
}

bool string_view_all_graph(String_View sv) {
    for (size_t i=0; i<sv.count; i++) {
        if (!isgraph(sv.str[i])) return false;
    }
    return true;
}

// see https://en.wikipedia.org/wiki/Percent-encoding
bool is_reserved(char c) {
    UNUSED(c);
    UNIMPLEMENTED("is_reserved");
}

bool is_unreserverd(char c) {
    if ('a' <= c && c <= 'z') return true;
    if ('A' <= c && c <= 'Z') return true;
    if ('0' <= c && c <= '9') return true;
    if (c == '-') return true;
    if (c == '_') return true;
    if (c == '~') return true;
    if (c == '.') return true;
    return false;
}

String_View percent_encode(Arena *a, String_View in) {
    Arena_String_Builder sb = {0};
    for (size_t i=0; i<in.count; i++) {
        char c = in.str[i];
        if (is_unreserverd(c)) {
            arena_da_append(a, &sb, c);
        } else if (c == ' ') {
            arena_sb_append_cstr(a, &sb, "%20");
        } else if (is_reserved(c)) {
            UNIMPLEMENTED("percent_encode");
        } else {
            UNIMPLEMENTED("percent_encode");
        }
    }
    return string_view_from_arena_string_builder(sb);
}

#define THUMBS_UP_SERIALIZED "[ { \"type\": \"emoji\", \"emoji\" : \"\U0001f44d\" } ]"

String_View build_url(Arena *a, Tg_Method_Call *call) {
    Arena_String_Builder sb = {0};
    arena_sb_append_cstr(a, &sb, URL_PREFIX);
    arena_sb_append_cstr(a, &sb, call->bot_token);
    arena_sb_append_cstr(a, &sb, "/");

    switch (call->method) {
        case GET_ME:
            arena_sb_append_cstr(a, &sb, "getMe");
            break;
        case GET_UPDATES:
            arena_sb_append_cstr(a, &sb, "getUpdates");
            break;
        case SEND_MESSAGE:
            {
                Arena temp = {0};
                String_View text_enc = percent_encode(&temp, string_view_from_char_ptr(call->text));

                arena_sb_append_cstr(a, &sb, "sendMessage");
                arena_sb_append_cstr(a, &sb, "?");
                char *chat_id_str = arena_sprintf(a, "chat_id=%ld", call->chat_id);
                arena_sb_append_cstr(a, &sb, chat_id_str);
                arena_sb_append_cstr(a, &sb, "&");
                char *text_str = arena_sprintf(a, "text=%.*s", (int) text_enc.count, text_enc.str);
                arena_sb_append_cstr(a, &sb, text_str);

                arena_free(&temp);
                break;
            }
        case SET_MESSAGE_REACTION:
            {
                Arena temp = {0};
                String_View emoji_enc = percent_encode(&temp, string_view_from_char_ptr(THUMBS_UP_SERIALIZED));

                arena_sb_append_cstr(a, &sb, "setMessageReaction");
                arena_sb_append_cstr(a, &sb, "?");
                char *chat_id_str = arena_sprintf(a, "chat_id=%ld", call->chat_id);
                arena_sb_append_cstr(a, &sb, chat_id_str);
                arena_sb_append_cstr(a, &sb, "&");
                char *message_id_str = arena_sprintf(a, "message_id=%d", call->message_id);
                arena_sb_append_cstr(a, &sb, message_id_str);
                arena_sb_append_cstr(a, &sb, "&");
                char *reaction_str = arena_sprintf(a, "reaction=%.*s", (int) emoji_enc.count, emoji_enc.str);
                arena_sb_append_cstr(a, &sb, reaction_str);

                arena_free(&temp);
                break;
            }
    };
    return string_view_from_arena_string_builder(sb);
}

/******************************
 * stack_*                    *
 ******************************/

void stack_push_string(String_View sv) {
    char *ptr = malloc((sv.count + 1) * sizeof(char));
    strncpy(ptr, sv.str, sv.count);
    ptr[sv.count] = '\0';

    assert(stack_count < MAX_STACK_SIZE);
    stack[stack_count].kind  = STACK_VALUE_STRING;
    stack[stack_count].str   = ptr;
    stack[stack_count].count = sv.count;
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
    switch (STACK_TOP.kind) {
        case STACK_VALUE_STRING:
            free(STACK_TOP.str);
            break;
        case STACK_VALUE_INT:
            break;
    }
    stack_count--;
}

bool stack_int() {
    if (stack_count < 1) return false;
    return STACK_TOP.kind == STACK_VALUE_INT;
}

bool stack_string() {
    if (stack_count < 1) return false;
    size_t i = stack_count-1;
    return stack[i].kind == STACK_VALUE_STRING;
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
            case STACK_VALUE_STRING: printf("%.*s, ", (int) stack[i].count, stack[i].str); break;
            case STACK_VALUE_INT:    printf("%d, ", stack[i].x); break;
        }
    }
    if (stack_count > 0) {
        size_t i = stack_count-1;
        switch (stack[i].kind) {
            case STACK_VALUE_STRING: printf("%.*s", (int) stack[i].count, stack[i].str); break;
            case STACK_VALUE_INT:    printf("%d", stack[i].x); break;
        }
    }
    printf("]\n");
}

#define FIFO_NAME "input-fifo"

int make_and_open_fifo() {
    if (mkfifo(FIFO_NAME, 0666) < 0) {
        printf("[ERROR] Could not make fifo '%s': %s\n", FIFO_NAME, strerror(errno));
        return -1;
    }
    int fd = open(FIFO_NAME, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("[ERROR] Could not open file '%s': %s\n", FIFO_NAME, strerror(errno));
        return -1;
    }
    return fd;
}

bool close_and_unlink_fifo(int fd) {
    if (close(fd) < 0) {
        printf("[ERROR] Could not close file: %s\n", strerror(errno));
        return false;
    }
    if (unlink(FIFO_NAME) < 0) {
        printf("[ERROR] Could not unlink file: %s\n", strerror(errno));
        return false;
    }
    return true;
}

/******************************
 * result_*                   *
 ******************************/

#define RESULT_DONE    (Result) {.state = STATE_DONE,    .kind = RESULT_KIND_VOID}
#define RESULT_PENDING (Result) {.state = STATE_PENDING, .kind = RESULT_KIND_VOID}
#define RESULT_ERROR   (Result) {.state = STATE_ERROR,   .kind = RESULT_KIND_VOID}

Result result_bool(bool b) {
    Result r = RESULT_DONE;
    r.kind = RESULT_KIND_BOOL;
    r.bool_val = b;
    return r;
}

Result result_int(int x) {
    Result r = RESULT_DONE;
    r.kind = RESULT_KIND_INT;
    r.x = x;
    return r;
}

Result result_string_view(String_View sv) {
    Result r = RESULT_DONE;
    r.kind = RESULT_KIND_STRING_VIEW;
    r.string_view = sv;
    return r;
}

Result result_json_value(json_value_t *val) {
    Result r = RESULT_DONE;
    r.kind = RESULT_KIND_JSON_VALUE;
    r.json_value = val;
    return r;
}

/******************************
 * context_*                  *
 ******************************/

Context context_new() {
    Context c = {
        .multi_handle = NULL,
        .easy_handle = NULL,
        .arena = NULL,
        .file_descriptor = -1,
    };
    for (size_t i=0; i<CONTEXT_KIND_COUNT; i++) {
        c.flag[i] = false;
    }
    return c;
}

bool context_is_empty(Context *c) {
    for (size_t i=0; i<CONTEXT_KIND_COUNT; i++) {
        if (c->flag[i]) return false;
    }
    return true;
}

void context_add_fifo(Context *c) {
    c->flag[CONTEXT_KIND_FIFO] = true;
    c->file_descriptor = make_and_open_fifo();
}

bool context_remove_fifo(Context *c) {
    bool success = close_and_unlink_fifo(c->file_descriptor);
    c->flag[CONTEXT_KIND_FIFO] = false;
    return success;
}

void context_add_curl_global(Context *c) {
    CURLcode r = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (r != 0) {
        UNIMPLEMENTED("context_add_curl_global");
    }
    c->flag[CONTEXT_KIND_CURL_GLOBAL] = true;
}

void context_remove_curl_global(Context *c) {
    curl_global_cleanup();
    c->flag[CONTEXT_KIND_CURL_GLOBAL] = false;
}

void context_add_curl_multi(Context *c) {
    c->multi_handle = curl_multi_init();
    if (c->multi_handle == NULL) {
        UNIMPLEMENTED("context_add_curl_multi");
    }
    c->flag[CONTEXT_KIND_CURL_MULTI] = true;
}

void context_remove_curl_multi(Context *c) {
    CURLMcode code = curl_multi_cleanup(c->multi_handle);
    if (code != CURLM_OK) {
        UNIMPLEMENTED("context_remove_curl_multi");
    }
    c->flag[CONTEXT_KIND_CURL_MULTI] = false;
}

void context_add_curl_easy(Context *c) {
    c->flag[CONTEXT_KIND_CURL_EASY] = true;
    c->easy_handle = curl_easy_init();
    assert(c->easy_handle != NULL);
}

void context_remove_curl_easy(Context *c) {
    curl_easy_cleanup(c->easy_handle);
    c->flag[CONTEXT_KIND_CURL_EASY] = false;
}

void context_add_arena(Context *c, Arena *a) {
    assert(a != NULL);
    c->arena = a;
    c->flag[CONTEXT_KIND_ARENA] = true;
}

void context_remove_arena(Context *c) {
    arena_free(c->arena);
    c->flag[CONTEXT_KIND_ARENA] = false;
}

/******************************
 * task_*                     *
 ******************************/

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

Task *task_const(Result r) {
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_CONST;
    ret->const_result = r;
    return ret;
}

// TODO: do we need this task kind?
Task *task_log(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_STRING_VIEW);
    Task *ret = task_alloc();
    ret->kind = TASK_KIND_LOG;
    UNIMPLEMENTED("task_log");
    return ret;
}

void task_par_append(Task *p, Task *t) {
    assert(p->kind == TASK_KIND_PARALLEL);
    assert(p->par_count < MAX_PAR_COUNT);

    p->par[p->par_count] = t;
    p->sub_ctx[p->par_count] = context_new();
    p->par_count++;
}

Task *task_then(Task *fst, Then_Function f) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_THEN;
    t->fst = fst;
    t->snd = NULL;
    t->then = f;
    return t;
}

Task *task_file_context(Task *body) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_CONTEXT;
    t->context_kind = CONTEXT_KIND_FIFO;
    t->context_body = body;
    return t;
}

Task *task_curl_easy_context(Task *body) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_CONTEXT;
    t->context_kind = CONTEXT_KIND_CURL_EASY;
    t->context_body = body;
    return t;
}

Task *task_curl_multi_context(Task *body) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_CONTEXT;
    t->context_kind = CONTEXT_KIND_CURL_MULTI;
    t->context_body = body;
    return t;
}

Task *task_curl_global_context(Task *body) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_CONTEXT;
    t->context_kind = CONTEXT_KIND_CURL_GLOBAL;
    t->context_body = body;
    return t;
}

Task *task_curl_perform(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_STRING_VIEW);

    Task *t = task_alloc();
    t->kind = TASK_KIND_CURL_PERFORM;
    t->url = r.string_view;
    t->curl_perform_sb = (Arena_String_Builder) {0};
    return t;
}

Task *task_parse_json_value(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_STRING_VIEW);

    Task *t = task_alloc();
    t->kind = TASK_KIND_PARSE_JSON_VALUE;
    t->json_source_str = r.string_view;
    return t;
}

Task *task_get_tg_user(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_JSON_VALUE);

    Task *t = task_alloc();
    t->kind = TASK_KIND_GET_TG_USER;
    t->json_root = r.json_value;
    return t;
}

Task *task_get_tg_update_list(Result r) {
    assert(r.state == STATE_DONE);
    assert(r.kind == RESULT_KIND_JSON_VALUE);

    Task *t = task_alloc();
    t->kind = TASK_KIND_GET_TG_UPDATE_LIST;
    t->json_root = r.json_value;
    return t;
}

Task *task_context_arena(Task *body, Arena arena) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_CONTEXT;
    t->context_kind = CONTEXT_KIND_ARENA;
    t->context_body = body;

    t->context_arena = arena;

    return t;
}

Task *task_call_getme(String_View url) {
    Arena a = {0};
    String_View url_copy = {
        .str = arena_memdup(&a, url.str, url.count),
        .count = url.count,
    };
    return task_curl_easy_context( 
            task_context_arena(
                task_then(
                    task_then(
                        task_then(task_const(result_string_view(url_copy)), task_curl_perform),
                        task_parse_json_value
                        ),
                    task_get_tg_user
                ),
                a
                )
            ); 
}

Task *task_call_getupdates(String_View url) {
    Arena a = {0};
    String_View url_copy = {
        .str = arena_memdup(&a, url.str, url.count),
        .count = url.count,
    };
    return task_curl_easy_context( 
            task_context_arena(
                task_then(
                    task_then(
                        task_then(task_const(result_string_view(url_copy)), task_curl_perform),
                        task_parse_json_value
                        ),
                    task_get_tg_update_list
                ),
                a
                )
            ); 
}

Task *runner = NULL;

Reply_Kind command_execute(Command c) {
    switch (c) {
        case HELP:
            printf("[HELP] The following commands are accepted:\n");
            for (Command i=0; i<COMMAND_COUNT; i++) {
                printf("[HELP] \"%s\"\n", command_keyword[i]);
                printf("[HELP]     Stack: %s\n", command_stack_config[i]);
                printf("[HELP]     Description: %s\n", command_description[i]);
            }
            return REPLY_ACK;
        case QUIT:
            return REPLY_CLOSE;
        case PRINT:
            stack_print();
            return REPLY_ACK;
        case DROP:
            stack_drop();
            return REPLY_ACK;
        case CLEAR:
            while (stack_count > 0) stack_drop();
            return REPLY_ACK;
        case TG_GETME:
            if (stack_string()) {
                Arena temp = {0};

                Tg_Method_Call call = new_tg_api_call_get_me(STACK_TOP.str);
                String_View url = build_url(&temp, &call);
                task_par_append(runner, task_curl_multi_context(task_call_getme(url)));
                stack_drop();

                arena_free(&temp);
                return REPLY_ACK;
            }
            return REPLY_ERROR;
        case TG_GETUPDATES:
            if (stack_string()) {
                Arena temp = {0};

                Tg_Method_Call call = new_tg_api_call_get_updates(STACK_TOP.str);
                String_View url = build_url(&temp, &call);
                task_par_append(runner, task_curl_multi_context(task_call_getupdates(url)));
                stack_drop();

                arena_free(&temp);
                return REPLY_ACK;
            }
            return REPLY_ERROR;
        case PLUS:
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x += x;
                return REPLY_ACK;
            }
            return REPLY_ERROR;
        case MINUS:
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x -= x;
                return REPLY_ACK;
            }
            return REPLY_ERROR;
        case TIMES:
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x *= x;
                return REPLY_ACK;
            }
            return REPLY_ERROR;
        case DIVIDE:
            if (stack_two_int()) {
                int x = stack[stack_count-1].x;
                stack_drop();
                stack[stack_count-1].x /= x;
                return REPLY_ACK;
            }
            return REPLY_ERROR;
        case COMMAND_COUNT:
            UNREACHABLE("COMMAND_COUNT is not a valid Command");
    }
    UNREACHABLE("no valid Command");
}

Reply_Kind execute(String_View prog) {
    for (prog = string_view_drop_ws(prog); prog.count > 0; prog = string_view_drop_ws(string_view_drop_non_ws(prog))) {
        String_View token = string_view_take_non_ws(prog);
        int val;
        if (string_view_try_parse_int(token, &val)) {
            stack_push_int(val);
        } else if (string_view_all_graph(token)) {
            bool matched = false;
            for (Command i=0; i<COMMAND_COUNT && !matched; i++) {
                // TODO: if there are commands that have the same prefix this can lead to errors
                if (strncmp(token.str, command_keyword[i], token.count) == 0) {
                    matched = true;
                    Reply_Kind r = command_execute(i);
                    switch (r) {
                        case REPLY_ACK:
                            break;
                        case REPLY_CLOSE:
                        case REPLY_ERROR:
                            return r;
                    }
                }
            }
            if (!matched) stack_push_string(token);
        } else {
            return REPLY_ERROR;
        }
    }
    return REPLY_ACK;
}

json_value_t *json_element_by_key(json_object_t *obj, const char *name) {
    for (json_object_element_t *elem = obj->start; elem != NULL; elem = elem->next) {
        if (strncmp(name, elem->name->string, elem->name->string_size) == 0) {
            return elem->value;
        }
    }
    return NULL;
}

bool as_tg_user(json_value_t *value, Tg_User *user) {
    json_object_t *object = json_value_as_object(value);
    if (object == NULL) return false;

    json_value_t *value_first_name = json_element_by_key(object, "first_name");
    if (value_first_name == NULL) return false;
    json_string_t *string_first_name = json_value_as_string(value_first_name);
    if (string_first_name == NULL) return false;
    user->first_name = string_first_name->string;
    return true;
}

bool as_tg_chat(json_value_t *value, Tg_Chat *chat) {
    json_object_t *object = json_value_as_object(value);
    if (object == NULL) return false;

    json_value_t *id_value = json_element_by_key(object, "id");
    if (id_value == NULL) return false;
    json_number_t *id_number = json_value_as_number(id_value);
    if (id_number == NULL) return false;
    char *endptr;
    chat_id_t id = strtol(id_number->number, &endptr, 10);
    assert(endptr[0] == '\0');
    chat->id = id;

    return true;
}

Tg_Message *as_tg_message(Arena *a, json_value_t *value) {
    json_object_t *object = json_value_as_object(value);
    if (object == NULL) return NULL;

    Tg_Message result;
    {
        json_value_t *id_value = json_element_by_key(object, "message_id");
        if (id_value == NULL) return NULL;
        json_number_t *id_number = json_value_as_number(id_value);
        if (id_number == NULL) return NULL;
        char *endptr;
        message_id_t id = strtol(id_number->number, &endptr, 10);
        assert(endptr[0] == '\0');
        result.message_id = id;
    }

    {
        json_value_t *chat_value = json_element_by_key(object, "chat");
        if (chat_value == NULL) return NULL;
        result.chat = arena_alloc(a, sizeof(Tg_Chat));
        if (!as_tg_chat(chat_value, result.chat)) return NULL;
    }
    
    {
        result.from = NULL;
        json_value_t *from_value = json_element_by_key(object, "from");
        if (from_value != NULL) {
            result.from = arena_alloc(a, sizeof(Tg_User));
            if (!as_tg_user(from_value, result.from)) return NULL;
        }
    }
    
    {
        result.text = NULL;
        json_value_t *text_value = json_element_by_key(object, "text");
        if (text_value != NULL) {
            json_string_t *text_string = json_value_as_string(text_value);
            if (text_string == NULL) return NULL;
            result.text = text_string->string;
        }
    }

    return arena_memdup(a, &result, sizeof(result));
}

Tg_Update *as_tg_update(Arena *a, json_value_t *value) {
    json_object_t *as_obj = json_value_as_object(value);
    if (as_obj == NULL) return NULL;

    Tg_Update result;

    json_value_t *id_value = json_element_by_key(as_obj, "update_id");
    if (id_value == NULL) return NULL;
    json_number_t *id_number = json_value_as_number(id_value);
    if (id_number == NULL) return NULL;
    char *endptr;
    update_id_t id = strtol(id_number->number, &endptr, 10);
    assert(endptr[0] == '\0');
    result.update_id = id;

    json_value_t *message_value = json_element_by_key(as_obj, "message");
    // message is an optional field
    result.message = NULL;
    if (message_value != NULL) {
        result.message = as_tg_message(a, message_value);
        if (result.message == NULL) return NULL;
    }

    return arena_memdup(a, &result, sizeof(result));
}

// TODO: multiple read tasks can use this so every read task should have its own
#define READ_BUF_CAPACITY 64
char read_buf[READ_BUF_CAPACITY];

void task_destroy(Task *t) {
    if (task_in_pool(t)) task_free(t);
}

size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t real_size = size * nmemb;

    Arena_String_Builder *sb = (Arena_String_Builder *) userdata;
    arena_da_append_many(sb->arena, sb, ptr, real_size);

    return real_size;
}

void *json_parse_cb(void *arena, size_t size) {
    return arena_alloc(arena, size);
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
                        break;
                    case STATE_ERROR:
                        UNIMPLEMENTED("task_poll");
                }
                return RESULT_PENDING;
            }
        case TASK_KIND_PARALLEL:
            {
                if (t->par_count == 0) return RESULT_DONE;
                Context *sub_ctx = t->sub_ctx + t->par_index;
                if (context_is_empty(sub_ctx)) {
                    // each subtask needs a copy of the context in case it will layer more context on top
                    *sub_ctx = *ctx;
                }
                Result r = task_poll(t->par[t->par_index], t->sub_ctx + t->par_index);
                switch (r.state) {
                    case STATE_ERROR:
                    case STATE_DONE:
                        task_destroy(t->par[t->par_index]);
                        t->par[t->par_index] = t->par[t->par_count - 1];
                        t->sub_ctx[t->par_index] = t->sub_ctx[t->par_count - 1];
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
                        task_destroy(t->fst);
                        t->snd = t->then(r);
                        break;
                    case STATE_PENDING:
                        break;
                    case STATE_ERROR:
                        return r;
                }
                return RESULT_PENDING;
            }
            Result r = task_poll(t->snd, ctx);
            switch (r.state) {
                case STATE_DONE:
                    task_destroy(t->snd);
                    break;
                case STATE_PENDING:
                    break;
                case STATE_ERROR:
                    break;
            }
            return r;
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
                        case STATE_ERROR:
                            UNIMPLEMENTED("task_poll");
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
                        case STATE_ERROR:
                            UNIMPLEMENTED("task_poll");
                    }
                    UNIMPLEMENTED("task_poll");
            }
            UNREACHABLE("invalid phase");
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
            printf("[LOG] %.*s\n", (int) t->log_msg.count, t->log_msg.str);
            return RESULT_DONE;
        case TASK_KIND_FIFO_REPL:
            {
                assert(ctx->flag[CONTEXT_KIND_FIFO]);
                ssize_t r = read(ctx->file_descriptor, read_buf, READ_BUF_CAPACITY-1);
                if (r == 0) {
                    return RESULT_PENDING;
                } else if (r == -1 && errno == EAGAIN) {
                    return RESULT_PENDING;
                } else if (r > 0) {
                    assert(r < READ_BUF_CAPACITY);
                    read_buf[r] = '\0';
                    // TODO: when a command is longer than READ_BUF_CAPACITY only a part of the command is passed to execute
                    switch (execute(string_view_from_char_ptr(read_buf))) {
                        case REPLY_CLOSE:
                            return RESULT_DONE;
                        case REPLY_ACK:
                            return RESULT_PENDING;
                        case REPLY_ERROR:
                            printf("[ERROR] Command caused error, try again\n");
                            return RESULT_PENDING;
                    }
                    UNREACHABLE("invalid Result_Kind");
                } else {
                    printf("[ERROR] Could not read from file: %s\n", strerror(errno));
                    return RESULT_ERROR;
                }
            }
        case TASK_KIND_CONTEXT:
            switch (t->context_kind) {
                case CONTEXT_KIND_ARENA:
                    {
                        if (!ctx->flag[CONTEXT_KIND_ARENA]) {
                            context_add_arena(ctx, &t->context_arena);
                        }
                        Result r = task_poll(t->context_body, ctx);
                        switch (r.state) {
                            case STATE_ERROR:
                            case STATE_DONE:
                                task_destroy(t->context_body);
                                context_remove_arena(ctx);
                                break;
                            case STATE_PENDING:
                                break;
                        }
                        return r;
                    }
                case CONTEXT_KIND_FIFO:
                    if (!ctx->flag[CONTEXT_KIND_FIFO]) {
                        context_add_fifo(ctx);
                        if (ctx->file_descriptor < 0) return RESULT_ERROR;
                        printf("[INFO] opened fifo successfully\n");
                        return RESULT_PENDING;
                    }
                    Result ret = task_poll(t->context_body, ctx);
                    switch (ret.state) {
                        case STATE_ERROR:
                        case STATE_DONE:
                            task_destroy(t->context_body);
                            if (!context_remove_fifo(ctx)) return RESULT_ERROR;
                            printf("[INFO] closed fifo successfully\n");
                            break;
                        case STATE_PENDING:
                            break;
                    }
                    return ret;
                case CONTEXT_KIND_CURL_GLOBAL:
                    {
                        if (!ctx->flag[CONTEXT_KIND_CURL_GLOBAL]) {
                            context_add_curl_global(ctx);
                        }
                        Result r = task_poll(t->context_body, ctx);
                        switch (r.state) {
                            case STATE_ERROR:
                            case STATE_DONE:
                                task_destroy(t->context_body);
                                context_remove_curl_global(ctx);
                                break;
                            case STATE_PENDING:
                                break;
                        }
                        return r;
                    }
                case CONTEXT_KIND_CURL_MULTI:
                    {
                        assert(ctx->flag[CONTEXT_KIND_CURL_GLOBAL]);
                        if (!ctx->flag[CONTEXT_KIND_CURL_MULTI]) {
                            context_add_curl_multi(ctx);
                        }
                        assert(ctx->multi_handle != NULL);
                        Result r = task_poll(t->context_body, ctx);
                        switch (r.state) {
                            case STATE_ERROR:
                            case STATE_DONE:
                                task_destroy(t->context_body);
                                context_remove_curl_multi(ctx);
                                break;
                            case STATE_PENDING:
                                break;
                        }
                        return r;
                    }
                case CONTEXT_KIND_CURL_EASY:
                    assert(ctx->flag[CONTEXT_KIND_CURL_GLOBAL]);
                    if (ctx->flag[CONTEXT_KIND_CURL_MULTI]) {
                        if (!ctx->flag[CONTEXT_KIND_CURL_EASY]) {
                            context_add_curl_easy(ctx);
                            CURLMcode code = curl_multi_add_handle(ctx->multi_handle, ctx->easy_handle);
                            if (code != CURLM_OK) {
                                UNIMPLEMENTED("task_poll");
                            }
                        }
                        assert(t->context_body != NULL);
                        Result r = task_poll(t->context_body, ctx);
                        switch (r.state) {
                            case STATE_ERROR:
                            case STATE_DONE:
                                task_destroy(t->context_body);
                                CURLMcode code = curl_multi_remove_handle(ctx->multi_handle, ctx->easy_handle);
                                if (code != CURLM_OK) {
                                    UNIMPLEMENTED("task_poll");
                                }
                                context_remove_curl_easy(ctx);
                                break;
                            case STATE_PENDING:
                                break;
                        }
                        return r;
                    } else {
                        if (!ctx->flag[CONTEXT_KIND_CURL_EASY]) {
                            context_add_curl_easy(ctx);
                        }
                        assert(t->context_body != NULL);
                        Result r = task_poll(t->context_body, ctx);
                        switch (r.state) {
                            case STATE_ERROR:
                            case STATE_DONE:
                                task_destroy(t->context_body);
                                context_remove_curl_easy(ctx);
                                break;
                            case STATE_PENDING:
                                break;
                        }
                        return r;
                    }
                case CONTEXT_KIND_COUNT:
                    UNREACHABLE("CONTEXT_KIND_COUNT is not a valid Context_Kind");
            }
            UNREACHABLE("no valid Context_Kind");
        case TASK_KIND_CURL_PERFORM:
            assert(ctx->flag[CONTEXT_KIND_CURL_EASY]);
            assert(ctx->flag[CONTEXT_KIND_ARENA]);

            CURLcode code;
            assert(t->url.str != NULL);
            code = curl_easy_setopt(ctx->easy_handle, CURLOPT_URL, t->url);
            if (code != CURLE_OK) {
                printf("[ERROR] tried to set url '%.*s'\n", (int) t->url.count, t->url.str);
                printf("[ERROR] failed curl_easy_setopt: %s\n", curl_easy_strerror(code));
                return RESULT_ERROR;
            }
            code = curl_easy_setopt(ctx->easy_handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
            if (code != CURLE_OK) {
                printf("[ERROR] failed curl_easy_setopt: %s\n", curl_easy_strerror(code));
                return RESULT_ERROR;
            }

            t->curl_perform_sb.arena = ctx->arena;
            code = curl_easy_setopt(ctx->easy_handle, CURLOPT_WRITEDATA, &t->curl_perform_sb);
            if (code != CURLE_OK) {
                printf("[ERROR] failed curl_easy_setopt: %s\n", curl_easy_strerror(code));
                return RESULT_ERROR;
            }
            if (ctx->flag[CONTEXT_KIND_CURL_MULTI]) {
                int running_handles;
                CURLMcode mcode = curl_multi_perform(ctx->multi_handle, &running_handles);
                if (mcode != CURLM_OK) {
                    UNIMPLEMENTED("task_poll");
                }
                int msgs_left;
                CURLMsg *msg = curl_multi_info_read(ctx->multi_handle, &msgs_left);
                if (msg == NULL) {
                    assert(running_handles >= 1);
                    return RESULT_PENDING;
                } else {
                    assert(msg->msg == CURLMSG_DONE);
                    assert(msg->easy_handle == ctx->easy_handle);
                }
            } else {
                code = curl_easy_perform(ctx->easy_handle);
                if (code != CURLE_OK) {
                    printf("[ERROR] failed curl_easy_perform: %s\n", curl_easy_strerror(code));
                    return RESULT_ERROR;
                }
            }
            return result_string_view(string_view_from_arena_string_builder(t->curl_perform_sb));
        case TASK_KIND_PARSE_JSON_VALUE:
            assert(ctx->flag[CONTEXT_KIND_ARENA]);
            json_value_t *root = json_parse_ex(
                    t->json_source_str.str, t->json_source_str.count, 
                    json_parse_flags_default, 
                    json_parse_cb, 
                    ctx->arena, 
                    NULL
                    );
            if (root == NULL) {
                printf("[ERROR] Failed to parse json value\n");
                return RESULT_ERROR;
            }
            return result_json_value(root);
        case TASK_KIND_GET_TG_USER:
            if (t->json_root == NULL) {
                UNIMPLEMENTED("task_poll");
            } else {
                json_object_t *obj = json_value_as_object(t->json_root);
                if (obj == NULL) return RESULT_ERROR;
                json_value_t *value_ok = json_element_by_key(obj, "ok");
                if (value_ok == NULL) return RESULT_ERROR;
                if (json_value_is_true(value_ok)) {
                    json_value_t *value_result = json_element_by_key(obj, "result");
                    if (value_result == NULL) return RESULT_ERROR;

                    Tg_User user;
                    if (as_tg_user(value_result, &user)) {
                        printf("[INFO] User named '%s'\n", user.first_name);
                        return RESULT_DONE;
                    } else {
                        UNIMPLEMENTED("task_poll");
                    }
                } else if (json_value_is_false(value_ok)) {
                    UNIMPLEMENTED("task_poll");
                } else {
                    return RESULT_ERROR;
                }
                return RESULT_ERROR;
            }
        case TASK_KIND_GET_TG_UPDATE_LIST:
            assert(ctx->flag[CONTEXT_KIND_ARENA]);

            if (t->json_root == NULL) {
                UNIMPLEMENTED("task_poll");
            } else {
                json_object_t *obj = json_value_as_object(t->json_root);
                if (obj == NULL) return RESULT_ERROR;
                json_value_t *value_ok = json_element_by_key(obj, "ok");
                if (value_ok == NULL) return RESULT_ERROR;
                if (json_value_is_true(value_ok)) {
                    json_value_t *value_result = json_element_by_key(obj, "result");
                    if (value_result == NULL) return RESULT_ERROR;

                    json_array_t *array_result = json_value_as_array(value_result);
                    if (array_result == NULL) return RESULT_ERROR;
                    size_t l = array_result->length;
                    Tg_Update *update_list[l];
                    json_array_element_t *update_elem = array_result->start;
                    for (size_t i=0; i<l; i++) {
                        update_list[i] = as_tg_update(ctx->arena, update_elem->value);
                        if (update_list[i] == NULL) return RESULT_ERROR;
                        update_elem = update_elem->next;
                    }
                    printf("[INFO] got %zu updates\n", l);
                    return RESULT_DONE;
                } else if (json_value_is_false(value_ok)) {
                    UNIMPLEMENTED("task_poll");
                } else {
                    UNIMPLEMENTED("task_poll");
                }
            }
    }
    UNREACHABLE("task_poll");
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

Task *task_iterate(Task *start, Then_Function next, Then_Function cond) {
    Task *t = task_alloc();
    t->kind = TASK_KIND_ITERATE;
    t->iter_phase = 0;
    t->iter_body = start;
    t->iter_next = next;
    t->iter_build_condition = cond;
    return t;
}

Task *repl() {
    Task *repl = task_alloc();
    repl->kind = TASK_KIND_FIFO_REPL;

    return repl;
}

#ifndef TEST

int main() {
    // We need to do this to initialize the pool allocator
    task_free_all();

    // runner is a global task of kind PARALLEL that all can acces
    runner = task_parallel();
    Task *runner_ctx = task_curl_global_context(runner);

    Task *fifo = task_file_context(repl());
    task_par_append(runner, fifo);

    Context ctx = context_new();

    printf("[INFO] starting server\n");
    Result r = task_poll(runner_ctx, &ctx);
    while (r.state != STATE_DONE) {
        r = task_poll(runner_ctx, &ctx);
    }
    printf("[INFO] finishing server\n");
    
    size_t count = 0;
    for (Task_Free_Node *i = task_pool_head; i != NULL; i = i->next) count++;
    printf("[INFO] memory leaked %zu tasks from the pool\n", TASK_POOL_CAPACITY - count);

    printf("[INFO] Stack: ");
    stack_print();
}

#endif // TEST
