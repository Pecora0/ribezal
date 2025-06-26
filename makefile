all: build/ribezal

run: build/ribezal
	./build/ribezal &

build/ribezal: ribezal.c devutils.h
	gcc -Wall -Wextra -Werror -o build/ribezal ribezal.c -lcurl
