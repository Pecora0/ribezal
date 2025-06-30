#ifndef TGAPI_H
#define TGAPI_H

#include <stdint.h>

#define URL_PREFIX "https://api.telegram.org/bot"

typedef struct {
    // REQUIRED
    char *first_name;
} Tg_User;

typedef int64_t chat_id_t;

typedef struct {
    // REQUIRED
    chat_id_t id;
} Tg_Chat;

typedef int32_t message_id_t;

typedef struct {
    // REQUIRED
    message_id_t message_id;
    Tg_Chat *chat;
    // OPTIONAL
    Tg_User *from;
    char *text;
} Tg_Message;

typedef int32_t update_id_t;

typedef struct {
    // REQUIRED
    update_id_t update_id;
    // OPTIONAL
    Tg_Message *message;
} Tg_Update;

typedef enum {
    GET_ME,
    GET_UPDATES,
    SEND_MESSAGE,
    SET_MESSAGE_REACTION,
} Tg_Method;

typedef struct {
    char *bot_token;
    Tg_Method method;
    // REQUIRED for: SEND_MESSAGE, SET_MESSAGE_REACTION
    chat_id_t chat_id;
    // REQUIRED for: SEND_MESSAGE
    char *text;
    // REQUIRED for: SET_MESSAGE_REACTION
    message_id_t message_id;
} Tg_Method_Call;

Tg_Method_Call new_tg_api_call_get_me(char *bot_token) {
    Tg_Method_Call result = {
        .bot_token = bot_token,
        .method = GET_ME,
    };
    return result;
}

Tg_Method_Call new_tg_api_call_get_updates(char *bot_token) {
    Tg_Method_Call result = {
        .bot_token = bot_token,
        .method = GET_UPDATES,
    };
    return result;
}

Tg_Method_Call new_tg_api_call_send_message(char *bot_token, Tg_Chat *chat, char *text) {
    Tg_Method_Call result = {
        .bot_token = bot_token,
        .method = SEND_MESSAGE,
        .chat_id = chat->id,
        .text = text,
    };
    return result;
}

Tg_Method_Call new_tg_api_call_set_message_reaction(char *bot_token, Tg_Message *message) {
    Tg_Method_Call result = {
        .bot_token = bot_token,
        .method = SET_MESSAGE_REACTION,
        .chat_id = message->chat->id,
        .message_id = message->message_id,
    };
    return result;
}

#endif // TGAPI_H
