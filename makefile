all: build/ribezal

run: build/ribezal
	./build/ribezal &

test: build/test
	./build/test

build/ribezal: ribezal.c devutils.h
	gcc -Wall -Wextra -Werror -o build/ribezal ribezal.c -lcurl

build/test: test.c thirdparty/utest.h
	gcc -Ithirdparty/ -o build/test test.c -lcurl
