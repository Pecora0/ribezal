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
- `help`:
    - Stack: (->)
    - Description: Prints documentation for the commands.
- `quit`:
    - Stack: (->)
    - Description: Closes this repl.
- `print`:
    - Stack: (->)
    - Description: Prints out the current stack.
- `drop`:
    - Stack: *tbd*
    - Description: Removes the top element from stack.
- `clear`:
    - Stack: *tbd*
    - Description: Removes all elements from stack.
- `request`:
    - Stack: (string ->)
    - Description: Performs Https-Request to the given URL.
- `tg-getMe`:
    - Stack: (string -> string)
    - Description: Constructs the URL for the Telegram method "getMe" out of the given bot token.
- `+`:
    - Stack: (int int -> int)
    - Description: Adds two numbers.
- `-`:
    - Stack: (int int -> int)
    - Description: Subtracts one number from the other.
- `*`:
    - Stack: (int int -> int)
    - Description: Multiplies two numbers.
- `/`:
    - Stack: (int int -> int)
    - Description: Divides one number by the other.

## Shoutouts

Some cool ressources I am using as inspiration:

- [c3fut](https://github.com/tsoding/c3fut): a futures implementation for the language C3 using interfaces
- [Pool Allocator](https://www.gingerbill.org/article/2019/02/16/memory-allocation-strategies-004/):
    excellent explanation of the principle of a pool allocator
