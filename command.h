#ifndef COMMAND_H
#define COMMAND_H

#include <assert.h>

typedef enum {
    HELP,
    QUIT,
    PRINT,
    DROP,
    CLEAR,
    PLUS,
    MINUS,
    TIMES,
    DIVIDE,
    TG_GETME,
    TG_GETUPDATES,
    COMMAND_COUNT,
} Command;

const char *command_keyword[] = {
    [HELP]          = "help",
    [QUIT]          = "quit",
    [PRINT]         = "print",
    [DROP]          = "drop",
    [CLEAR]         = "clear",
    [PLUS]          = "+", 
    [MINUS]         = "-", 
    [TIMES]         = "*", 
    [DIVIDE]        = "/",
    [TG_GETME]      = "tg-getMe",
    [TG_GETUPDATES] = "tg-getUpdates",
};
static_assert(sizeof(command_keyword) / sizeof(command_keyword[0]) == COMMAND_COUNT);

const char *command_stack_config[] = {
    [HELP]          = "(->)",
    [QUIT]          = "(->)",
    [PRINT]         = "(->)",
    [DROP]          = "*tbd*",
    [CLEAR]         = "*tbd*",
    [PLUS]          = "(int int -> int)",
    [MINUS]         = "(int int -> int)",
    [TIMES]         = "(int int -> int)",
    [DIVIDE]        = "(int int -> int)",
    [TG_GETME]      = "(string ->)",
    [TG_GETUPDATES] = "(string ->)",
};
static_assert(sizeof(command_stack_config) / sizeof(command_stack_config[0]) == COMMAND_COUNT);

const char *command_description[] = {
    [HELP]          = "Prints documentation for the commands.",
    [QUIT]          = "Closes this repl.",
    [PRINT]         = "Prints out the current stack.",
    [DROP]          = "Removes the top element from stack.",
    [CLEAR]         = "Removes all elements from stack.",
    [PLUS]          = "Adds two numbers.", 
    [MINUS]         = "Subtracts one number from the other.", 
    [TIMES]         = "Multiplies two numbers.", 
    [DIVIDE]        = "Divides one number by the other.",
    [TG_GETME]      = "Takes a bot token, performs a 'getMe' call to the telegram api and gives some informative output.",
    [TG_GETUPDATES] = "Takes a bot token, performs a 'getUpdates' call to the telegram api and gives some informative output.",
};
static_assert(sizeof(command_description) / sizeof(command_description[0]) == COMMAND_COUNT);

#endif // COMMAND_H
