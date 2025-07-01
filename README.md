# ribezal

Simple messaging server as an experimentation with asynchronous programming in C.

## Quickstart

Build with `make` and run `ribezal` preferably in background.

```console
$ make
$ ./ribezal &
```

You can send "messages" to the server by writing to `input-fifo`, e.g.:

```console
$ echo "your command" > input-fifo
```

Stop the server with command `quit`:

```console
$ echo "quit" > input-fifo
```

## Documentation

You can communicate with ribezal using a little stack based language.
(At the moment this is just a simple calculator).
Commands are separated by whitespace.
Integers are recognised as such and pushed to the stack.
All commands that are not integers or keywords are pushed as strings to the strings.

There are the following keywords:
- `quit`
- `print`
- `drop`
- `clear`
- `+`, `-`, `*`, `/`
- `request`
- `tg-getMe`

## Shoutouts

Some cool ressources I am using as inspiration:

- [c3fut](https://github.com/tsoding/c3fut): a futures implementation for the language C3 using interfaces
- [Pool Allocator](https://www.gingerbill.org/article/2019/02/16/memory-allocation-strategies-004/):
    excellent explanation of the principle of a pool allocator
