#include "utest.h"

#define TEST
#include "ribezal.c"

struct String_Builder_Fixture {
    String_Builder sb;
};

#define ASSERT_ZERO_TERMINATION(sb) do { ASSERT_LT((sb).count, (sb).capacity); ASSERT_EQ('\0', (sb).str[(sb).count]); } while(0)

UTEST_F_SETUP(String_Builder_Fixture) {
    utest_fixture->sb = string_builder_new();

    EXPECT_NE(utest_fixture->sb.str, NULL);
    ASSERT_EQ(utest_fixture->sb.count, 0);
    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
}

UTEST_F_TEARDOWN(String_Builder_Fixture) {
    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
    string_builder_destroy(utest_fixture->sb);
}

UTEST_F(String_Builder_Fixture, append) {
    char c = 'a';
    string_builder_append(&utest_fixture->sb, c);

    ASSERT_EQ(utest_fixture->sb.count, 1);
    ASSERT_EQ(utest_fixture->sb.str[0], c);
    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
}

UTEST_F(String_Builder_Fixture, append_str) {
    char *str = "test";
    string_builder_append_str(&utest_fixture->sb, str);

    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
    ASSERT_EQ(utest_fixture->sb.count, strlen(str));
    ASSERT_STREQ(utest_fixture->sb.str, str);
}

UTEST_F(String_Builder_Fixture, append_str_n) {
    char *str = "test";
    size_t n = strlen(str);
    string_builder_append_str_n(&utest_fixture->sb, str, n);

    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
    ASSERT_EQ(utest_fixture->sb.count, strlen(str));
    ASSERT_STREQ(utest_fixture->sb.str, str);
}

UTEST_F(String_Builder_Fixture, clear) {
    string_builder_append_str(&utest_fixture->sb, "fooBarBaz");
    string_builder_clear(&utest_fixture->sb);

    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
    ASSERT_EQ(utest_fixture->sb.count, strlen(""));
}

UTEST_F(String_Builder_Fixture, string_builder_printf) {
    char *format = "test %d %s\t";
    const size_t buffer_count = 16;
    char buffer[buffer_count];
    int n1 = sprintf(buffer, format, 42, "moin");
    assert(n1 < buffer_count);

    int n2 = string_builder_printf(&utest_fixture->sb, format, 42, "moin");
    ASSERT_EQ(n1, n2);
    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
    ASSERT_STREQ(buffer, utest_fixture->sb.str);
}

UTEST_F(String_Builder_Fixture, string_builder_appendf) {
    char *format = "test %d %s\t";
    const size_t buffer_count = 32;
    char buffer[buffer_count];
    int n1 = sprintf(buffer,      format, 42, "moin");
    n1    += sprintf(buffer + n1, format, 10, "hallo");
    assert(n1 < buffer_count);

    int n2 = string_builder_appendf(&utest_fixture->sb, format, 42, "moin");
    n2    += string_builder_appendf(&utest_fixture->sb, format, 10, "hallo");
    ASSERT_EQ(n1, n2);
    ASSERT_ZERO_TERMINATION(utest_fixture->sb);
    ASSERT_STREQ(buffer, utest_fixture->sb.str);
}

UTEST(Context, context_new) {
    Context ctx = context_new();
    ASSERT_TRUE(context_is_empty(&ctx));
}

struct Task_Const_Fixture {
    Context ctx;
    String_Builder sb;
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
                case RESULT_KIND_STRING:
                    ASSERT_EQ(pre.string, post.string);
                    ASSERT_STREQ(pre.string->str, post.string->str);
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

UTEST_F(Task_Const_Fixture, RESULT_KIND_STRING) {
    utest_fixture->sb = string_builder_new();
    string_builder_append_str(&utest_fixture->sb, "moin");
    string_builder_append(&utest_fixture->sb, '\0');

    utest_fixture->pre = result_string(&utest_fixture->sb);
}

UTEST_F(Task_Const_Fixture, RESULT_KIND_JSON_VALUE) {
    // We don't free this pointer...
    // It's fine when tests leak memory - Right?
    utest_fixture->pre = result_json_value(json_parse("[2, 3]", 6));
}

#define BOT_TOKEN "123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
struct Build_URL_Fixture {
    String_Builder sb;
    Tg_Method_Call call;
    char *expectation;
};

UTEST_F_SETUP(Build_URL_Fixture) {
    utest_fixture->sb = string_builder_new();
}

UTEST_F_TEARDOWN(Build_URL_Fixture) {
    build_url(&utest_fixture->sb, &utest_fixture->call);
    ASSERT_STREQ(utest_fixture->expectation, utest_fixture->sb.str);
}

UTEST_F(Build_URL_Fixture, getMe) {
    utest_fixture->call = new_tg_api_call_get_me(BOT_TOKEN);
    utest_fixture->expectation = URL_PREFIX BOT_TOKEN "/getMe";
}

UTEST_F(Build_URL_Fixture, getUpdates) {
    utest_fixture->call = new_tg_api_call_get_updates(BOT_TOKEN);
    utest_fixture->expectation = URL_PREFIX BOT_TOKEN "/getUpdates";
}

UTEST_F(Build_URL_Fixture, sendMessage) {
    Tg_Chat chat = {
        .id = 420,
    };
    utest_fixture->call = new_tg_api_call_send_message(BOT_TOKEN, &chat, "Lorem ipsum");
    utest_fixture->expectation = URL_PREFIX BOT_TOKEN "/sendMessage?chat_id=420&text=Lorem\%20ipsum";
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
    stack_push_string(str);
    ASSERT_EQ(stack_count, stack_count_pre+1);
    ASSERT_TRUE(stack_string());
    ASSERT_STREQ(str, STACK_TOP.str);

    stack_count = 0;
}

UTEST(execute, empty) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute("");
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count, stack_count_pre);

    stack_count = 0;
}

UTEST(execute, string) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute("hello");
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count, stack_count_pre + 1);
    ASSERT_TRUE(stack_string());

    stack_count = 0;
}

UTEST(execute, int) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute("123");
    ASSERT_EQ(r, REPLY_ACK);
    ASSERT_EQ(stack_count, stack_count_pre + 1);
    ASSERT_TRUE(stack_int());

    stack_count = 0;
}

UTEST(execute, quit) {
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute("quit");
    ASSERT_EQ(r, REPLY_CLOSE);
    ASSERT_EQ(stack_count, stack_count_pre);

    stack_count = 0;
}

UTEST(execute, drop) {
    stack_push_string("hello");
    stack_push_string("world");
    stack_push_int(-8);
    size_t stack_count_pre = stack_count;
    Reply_Kind r = execute("drop");
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
    Reply_Kind r = execute("+");
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
    Reply_Kind r = execute("-");
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
    Reply_Kind r = execute("*");
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
    Reply_Kind r = execute("/");
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
    String_Builder sb = string_builder_new();
    for (size_t i=0; i<PROGRAM_LIST_COUNT; i++) {
        string_builder_clear(&sb);
        string_builder_append_str(&sb, program_list[i].prog);
        string_builder_append_null(&sb);

        Reply_Kind r = execute(sb.str);
        ASSERT_EQ(r, REPLY_ACK);
        ASSERT_TRUE(stack_int());
        ASSERT_EQ(STACK_TOP.x, program_list[i].result);
    }
}

UTEST_MAIN()
