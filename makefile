all: build/ribezal README.md

run: build/ribezal
	rm -f input-fifo
	./build/ribezal &

test: build/test
	./build/test

clean:
	rm ./build/*

README.md: build/generate-readme
	./build/generate-readme

build/ribezal: ribezal.c devutils.h tgapi.h command.h
	gcc -Wall -Wextra -Werror -o build/ribezal ribezal.c -lcurl

build/test: ribezal.c test.c thirdparty/utest.h tgapi.h command.h
	gcc -Wall -Ithirdparty/ -o build/test test.c -lcurl

build/generate-readme: generate-readme.c command.h
	gcc -Wall -Wextra -Werror -o build/generate-readme generate-readme.c
