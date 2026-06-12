/**
 * AlgoForge — include/monitoring/telegram.hpp
 *
 * TelegramAlerter — sends messages to a Telegram chat via the Bot API.
 * Python oracle: python/algoforge/telegram.py.
 *
 * Credentials come from constructor args or the TELEGRAM_TOKEN / TELEGRAM_CHAT_ID
 * environment variables. With neither, the alerter silently disables itself — every
 * send()/alert*() returns false without throwing (Python parity).
 *
 * The HTTP POST is an injectable boundary (HttpPost) so the message-formatting logic
 * is parity-tested with a capturing sender; the default sender uses libcurl when
 * AF_HAS_LIBCURL is defined (else send() returns false — there is no stdlib HTTP in
 * C++ the way Python has urllib).
 */
#pragma once

#include <functional>
#include <string>

namespace algoforge::monitoring {

class TelegramAlerter {
public:
    /** (url, json_body) → success. Injected for tests; nullptr → built-in sender. */
    using HttpPost = std::function<bool(const std::string& url, const std::string& json_body)>;

    /** Bot API endpoint template; {token} is substituted in send_url(). */
    static constexpr const char* BASE_URL = "https://api.telegram.org/bot{token}/sendMessage";

    explicit TelegramAlerter(std::string bot_token = "",
                             std::string chat_id   = "",
                             HttpPost    sender     = nullptr);

    /** True when both token and chat-id are configured. */
    bool enabled() const noexcept { return enabled_; }

    /** The resolved sendMessage URL (BASE_URL with the token substituted). */
    std::string send_url() const;

    /** Send a raw message. parse_mode "" = plain text; "MarkdownV2"/"HTML" to format. */
    bool send(const std::string& message, const std::string& parse_mode = "");

    /** "[LEVEL] msg" (level uppercased). */
    bool alert(const std::string& level, const std::string& msg);
    /** Guard-state transition with a state emoji. */
    bool alert_guard_state(const std::string& state_name, const std::string& reason);
    /** Trade-placed notification (lots %.2f, prices %.5f, 📈/📉 by direction). */
    bool alert_trade(const std::string& symbol, const std::string& direction,
                     double lots, double entry, double sl, double tp);

    /** Pure JSON request body {"chat_id","text"[,"parse_mode"]} (exposed for parity tests). */
    static std::string build_payload(const std::string& chat_id,
                                     const std::string& message,
                                     const std::string& parse_mode);

private:
    std::string token_;
    std::string chat_id_;
    bool        enabled_;
    HttpPost    sender_;
};

} // namespace algoforge::monitoring
