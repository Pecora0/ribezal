all: ribezal

run: ribezal
	./ribezal &

ribezal: ribezal.c devutils.h
	gcc -Wall -Wextra -Werror -o ribezal ribezal.c -lcurl
