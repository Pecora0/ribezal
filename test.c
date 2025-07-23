#include "utest.h"

#define TEST
#include "ribezal.c"

UTEST(String_View, string_view_try_parse_int) {
    String_View test = string_view_from_char_ptr("161");
    int result;
    ASSERT_TRUE(string_view_try_parse_int(test, &result));
    ASSERT_EQ(161, result);

    test = string_view_from_char_ptr("");
    ASSERT_FALSE(string_view_try_parse_int(test, &result));
}

UTEST(Context, context_new) {
    Context ctx = context_new();
    ASSERT_TRUE(context_is_empty(&ctx));
}

struct Task_Const_Fixture {
    Context ctx;
    Result pre;
};

UTEST_F_SETUP(Task_Const_Fixture) {
    task_free_all();
    utest_fixture->ctx = context_new();
    utest_fixture->pre = RESULT_PENDING;
}

UTEST_F_TEARDOWN(Task_Const_Fixture) {
    Result pre = utest_fixture->pre;
    Task *t = task_const(pre);
    Result post = task_poll(t, &utest_fixture->ctx);

    ASSERT_EQ(pre.state, post.state);
    switch (pre.state) {
        case STATE_DONE:
            ASSERT_EQ(pre.kind, post.kind);
            switch (pre.kind) {
                case RESULT_KIND_VOID:
                    break;
                case RESULT_KIND_BOOL:
                    ASSERT_EQ(pre.bool_val, post.bool_val);
                    break;
                case RESULT_KIND_INT:
                    ASSERT_EQ(pre.x, post.x);
                    break;
                case RESULT_KIND_STRING_VIEW:
                    ASSERT_TRUE(false);
                    break;
                case RESULT_KIND_JSON_VALUE:
                    ASSERT_EQ(pre.json_value, post.json_value);
                    break;
            }
            break;
        case STATE_PENDING:
            ASSERT_TRUE(false);
        case STATE_ERROR:
            break;
    }
}

UTEST_F(Task_Const_Fixture, STATE_ERROR) {
    utest_fixture->pre = RESULT_ERROR;
}

UTEST_F(Task_Const_Fixture, RESULT_KIND_VOID) {
    utest_fixture->pre = RESULT_DONE;
}

UTEST_F(Task_Const_Fixture, RESULT_KIND_BOOL) {
    utest_fixture->pre = result_bool(true);
}

UTEST_F(Task_Const_Fixture, RESULT_KIND_INT) {
    utest_fixture->pre = result_int(42);
}

UTEST_F(Task_Const_Fixture, RESULT_KIND_JSON_VALUE) {
    // We don't free this pointer...
    // It's fine when tests leak memory - Right?
    utest_fixture->pre = result_json_value(json_parse("[2, 3]", 6));
}

#define BOT_TOKEN "123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
struct Build_URL_Fixture {
    Arena arena;
    Tg_Method_Call call;
    String_View expectation;
};

UTEST_F_SETUP(Build_URL_Fixture) {
    utest_fixture->arena = (Arena) {0};
}

UTEST_F_TEARDOWN(Build_URL_Fixture) {
    String_View url = build_url(&utest_fixture->arena, &utest_fixture->call);
    ASSERT_EQ(utest_fixture->expectation.count, url.count);
    ASSERT_STRNEQ(utest_fixture->expectation.str, url.str, url.count);
    arena_free(&utest_fixture->arena);
}

UTEST_F(Build_URL_Fixture, getMe) {
    utest_fixture->call = new_tg_api_call_get_me(BOT_TOKEN);
    utest_fixture->expectation = string_view_from_char_ptr(URL_PREFIX BOT_TOKEN "/getMe");
}

UTEST_F(Build_URL_Fixture, getUpdates) {
    utest_fixture->call = new_tg_api_call_get_updates(BOT_TOKEN);
    utest_fixture->expectation = string_view_from_char_ptr(URL_PREFIX BOT_TOKEN "/getUpdates");
}

UTEST_F(Build_URL_Fixture, sendMessage) {
    Tg_Chat chat = {
        .id = 420,
    };
    utest_fixture->call = new_tg_api_call_send_message(BOT_TOKEN, &chat, "Lorem ipsum");
    utest_fixture->expectation = string_view_from_char_ptr(URL_PREFIX BOT_TOKEN "/sendMessage?chat_id=420&text=Lorem\%20ipsum");
}

UTEST(stack, int) {
    int x = 42;

    size_t stack_count_pre = stack_count;
    stack_push_int(x);
    ASSERT_EQ(stack_count, stack_count_pre+1);
    ASSERT_TRUE(stack_int());
    ASSERT_EQ(x, STACK_TOP.x);

    stack_count = 0;
}

UTEST(stack, string) {
    char *str = "moin";

    size_t stack_count_pre = stack_count;
    stack_push_string(string_view_from_char_ptr(str));
    ASSERT_EQ(stack_count, stack_count_pre+1);
    ASSERT_TRUE(stack_string());
    ASSERT_EQ(strlen(str), STACK_TOP.sv.count);
    ASSERT_STRNEQ(str, STACK_TOP.sv.str, STACK_TOP.sv.count);

    stack_count = 0;
}

UTEST(execute, empty) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr(""));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count, stack_count_pre);

    stack_count = 0;
}

UTEST(execute, string) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("hello"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count, stack_count_pre + 1);
    ASSERT_TRUE(stack_string());

    stack_count = 0;
}

UTEST(execute, int) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("123"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count, stack_count_pre + 1);
    ASSERT_TRUE(stack_int());

    stack_count = 0;
}

UTEST(execute, quit) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("quit"));
    ASSERT_EQ(r, REPLY_CLOSE);
    ASSERT_EQ(stack_count, stack_count_pre);

    stack_count = 0;
}

UTEST(execute, drop) {
    stack_push_string(string_view_from_char_ptr("hello"));
    stack_push_string(string_view_from_char_ptr("world"));
    stack_push_int(-8);
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("drop"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count + 1, stack_count_pre);

    stack_count = 0;
}

UTEST(execute, plus) {
    int x = 2;
    int y = 3;

    stack_push_int(x);
    stack_push_int(y);
    ASSERT_TRUE(stack_two_int());
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("+"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count + 1, stack_count_pre);
    ASSERT_TRUE(stack_int());
    ASSERT_EQ(STACK_TOP.x, x + y);

    stack_count = 0;
}

UTEST(execute, minus) {
    int x = 2;
    int y = 3;

    stack_push_int(x);
    stack_push_int(y);
    ASSERT_TRUE(stack_two_int());
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("-"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count + 1, stack_count_pre);
    ASSERT_TRUE(stack_int());
    ASSERT_EQ(STACK_TOP.x, x - y);

    stack_count = 0;
}

UTEST(execute, times) {
    int x = 2;
    int y = 3;

    stack_push_int(x);
    stack_push_int(y);
    ASSERT_TRUE(stack_two_int());
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("*"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count + 1, stack_count_pre);
    ASSERT_TRUE(stack_int());
    ASSERT_EQ(STACK_TOP.x, x * y);

    stack_count = 0;
}

UTEST(execute, divide) {
    int x = 2;
    int y = 3;

    stack_push_int(x);
    stack_push_int(y);
    ASSERT_TRUE(stack_two_int());
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute(string_view_from_char_ptr("/"));
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count + 1, stack_count_pre);
    ASSERT_TRUE(stack_int());
    ASSERT_EQ(STACK_TOP.x, x / y);

    stack_count = 0;
}

typedef struct {
    const char *prog;
    int result;
} Arithmetic_Case;

Arithmetic_Case program_list[] = {
    {"2 3 +", 5},
    {"2 3 *", 6},
};
#define PROGRAM_LIST_COUNT (sizeof(program_list) / sizeof(program_list[0]))

UTEST(execute, arithmetic) {
    Arena temp = {0};
    Arena_String_Builder sb = {0};
    for (size_t i=0; i<PROGRAM_LIST_COUNT; i++) {
        sb.count = 0;
        arena_sb_append_cstr(&temp, &sb, program_list[i].prog);

        Reply_Kind r = execute(string_view_from_arena_string_builder(sb));
        ASSERT_EQ(r, REPLY_ACK);
        ASSERT_TRUE(stack_int());
        ASSERT_EQ(STACK_TOP.x, program_list[i].result);
    }
}

UTEST_MAIN()
