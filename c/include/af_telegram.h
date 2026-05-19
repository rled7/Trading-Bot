#ifndef AF_TELEGRAM_H
#define AF_TELEGRAM_H

typedef struct {
    char bot_token[128];
    char chat_id[32];
    int  enabled; /* 0 = disabled (no-op send) */
} af_telegram_t;

void af_telegram_init(af_telegram_t *tg, const char *bot_token, const char *chat_id);
int  af_telegram_send(const af_telegram_t *tg, const char *message);
int  af_telegram_alert(const af_telegram_t *tg, const char *level, const char *msg);

#endif /* AF_TELEGRAM_H */
