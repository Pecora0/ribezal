#include "utest.h"

#define TEST
#include "ribezal.c"

struct String_Builder_Fixture {
    String_Builder sb;
};

UTEST_F_SETUP(String_Builder_Fixture) {
    utest_fixture->sb = string_builder_new();

    EXPECT_NE(utest_fixture->sb.str, NULL);
    ASSERT_EQ(utest_fixture->sb.count, 0);
}

UTEST_F_TEARDOWN(String_Builder_Fixture) {
    EXPECT_LE(utest_fixture->sb.count, utest_fixture->sb.capacity);
    string_builder_destroy(utest_fixture->sb);
}

UTEST_F(String_Builder_Fixture, append) {
    char c = 'a';
    string_builder_append(&utest_fixture->sb, c);

    ASSERT_EQ(utest_fixture->sb.count, 1);
    ASSERT_EQ(utest_fixture->sb.str[0], c);
}

UTEST_F(String_Builder_Fixture, append_str) {
    char *str = "test";
    string_builder_append_str(&utest_fixture->sb, str);
    ASSERT_EQ(utest_fixture->sb.count, strlen(str));

    string_builder_append(&utest_fixture->sb, '\0');
    ASSERT_STREQ(utest_fixture->sb.str, str);
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

    ASSERT_EQ(pre.state, STATE_DONE);
    ASSERT_EQ(post.state, STATE_DONE);
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
        case RESULT_KIND_INT_TUPLE:
            ASSERT_EQ(pre.x, post.x);
            ASSERT_EQ(pre.y, post.y);
            break;
        case RESULT_KIND_STRING:
            ASSERT_EQ(pre.string, post.string);
            ASSERT_STREQ(pre.string->str, post.string->str);
            break;
    }
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

UTEST_F(Task_Const_Fixture, RESULT_KIND_INT_TUPLE) {
    utest_fixture->pre = result_int_tuple(42, -5);
}

UTEST_F(Task_Const_Fixture, RESULT_KIND_STRING) {
    utest_fixture->sb = string_builder_new();
    string_builder_append_str(&utest_fixture->sb, "moin");
    string_builder_append(&utest_fixture->sb, '\0');

    utest_fixture->pre = result_string(&utest_fixture->sb);
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
    ASSERT_STREQ(utest_fixture->sb.str, utest_fixture->expectation);
}

UTEST_F(Build_URL_Fixture, getMe) {
    utest_fixture->call = new_tg_api_call_get_me(BOT_TOKEN);
    utest_fixture->expectation = URL_PREFIX BOT_TOKEN "/getMe";
}

UTEST_F(Build_URL_Fixture, getUpdates) {
    utest_fixture->call = new_tg_api_call_get_updates(BOT_TOKEN);
    utest_fixture->expectation = URL_PREFIX BOT_TOKEN "/getUpdates";
}

UTEST_MAIN()
