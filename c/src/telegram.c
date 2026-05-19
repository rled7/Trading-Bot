#include "../include/af_telegram.h"

#include <stdio.h>
#include <string.h>

void af_telegram_init(af_telegram_t *tg, const char *bot_token, const char *chat_id)
{
    memset(tg, 0, sizeof(*tg));
    if (bot_token && bot_token[0]) {
        strncpy(tg->bot_token, bot_token, sizeof(tg->bot_token) - 1);
        tg->bot_token[sizeof(tg->bot_token) - 1] = '\0';
    }
    if (chat_id && chat_id[0]) {
        strncpy(tg->chat_id, chat_id, sizeof(tg->chat_id) - 1);
        tg->chat_id[sizeof(tg->chat_id) - 1] = '\0';
    }
    tg->enabled = (bot_token && bot_token[0] && chat_id && chat_id[0]) ? 1 : 0;
}

int af_telegram_send(const af_telegram_t *tg, const char *message)
{
    if (!tg || !tg->enabled) return 0;

    /* Sanitise message: replace " with ' and newlines with space */
    char safe[1024];
    size_t si = 0, mi = 0;
    while (message[mi] && si < sizeof(safe) - 1) {
        char c = message[mi++];
        if (c == '"') {
            safe[si++] = '\'';
        } else if (c == '\n' || c == '\r') {
            safe[si++] = ' ';
        } else {
            safe[si++] = c;
        }
    }
    safe[si] = '\0';

    /* Build and execute curl command */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -s -X POST https://api.telegram.org/bot%s/sendMessage "
        "-d chat_id=%s -d text=\"%s\" > /dev/null 2>&1",
        tg->bot_token, tg->chat_id, safe);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    pclose(fp);
    return 1;
}

int af_telegram_alert(const af_telegram_t *tg, const char *level, const char *msg)
{
    if (!tg || !tg->enabled) return 0;

    char formatted[1024];
    snprintf(formatted, sizeof(formatted), "[%s] %s", level ? level : "ALERT", msg ? msg : "");
    return af_telegram_send(tg, formatted);
}
