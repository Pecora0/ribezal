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

UTEST_MAIN()
