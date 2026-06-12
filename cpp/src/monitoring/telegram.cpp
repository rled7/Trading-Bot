/**
 * AlgoForge — src/monitoring/telegram.cpp
 * TelegramAlerter implementation. Python oracle: python/algoforge/telegram.py.
 *
 * The message formatting (alert / alert_guard_state / alert_trade / build_payload)
 * is pure and parity-tested. The default HTTP sender is a thin libcurl POST guarded
 * by AF_HAS_LIBCURL (untested by design, like the other socket boundaries).
 */
#include "monitoring/telegram.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#ifdef AF_HAS_LIBCURL
#include <curl/curl.h>
#endif

namespace algoforge::monitoring {

namespace {

std::string getenv_str(const char* key) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : std::string();
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    out += b;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string fmt(const char* spec, double v) {
    char b[64];
    std::snprintf(b, sizeof b, spec, v);
    return std::string(b);
}

// Default sender: a thin libcurl POST. Returns false when libcurl is unavailable.
bool default_post(const std::string& url, const std::string& json_body) {
#ifdef AF_HAS_LIBCURL
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK && status == 200;
#else
    (void)url; (void)json_body;
    return false;
#endif
}

} // namespace

TelegramAlerter::TelegramAlerter(std::string bot_token, std::string chat_id, HttpPost sender)
    : token_(!bot_token.empty() ? std::move(bot_token) : getenv_str("TELEGRAM_TOKEN")),
      chat_id_(!chat_id.empty() ? std::move(chat_id) : getenv_str("TELEGRAM_CHAT_ID")),
      enabled_(!token_.empty() && !chat_id_.empty()),
      sender_(std::move(sender)) {}

std::string TelegramAlerter::send_url() const {
    const std::string tmpl = BASE_URL;
    const std::string marker = "{token}";
    const auto pos = tmpl.find(marker);
    if (pos == std::string::npos) return tmpl;
    return tmpl.substr(0, pos) + token_ + tmpl.substr(pos + marker.size());
}

std::string TelegramAlerter::build_payload(const std::string& chat_id,
                                           const std::string& message,
                                           const std::string& parse_mode) {
    std::string out = "{\"chat_id\":\"" + json_escape(chat_id) +
                      "\",\"text\":\"" + json_escape(message) + "\"";
    if (!parse_mode.empty())
        out += ",\"parse_mode\":\"" + json_escape(parse_mode) + "\"";
    out += "}";
    return out;
}

bool TelegramAlerter::send(const std::string& message, const std::string& parse_mode) {
    if (!enabled_) return false;
    const std::string url     = send_url();
    const std::string payload = build_payload(chat_id_, message, parse_mode);
    return sender_ ? sender_(url, payload) : default_post(url, payload);
}

bool TelegramAlerter::alert(const std::string& level, const std::string& msg) {
    return send("[" + to_upper(level) + "] " + msg);
}

bool TelegramAlerter::alert_guard_state(const std::string& state_name, const std::string& reason) {
    const std::string s = to_upper(state_name);
    std::string emoji;
    if      (s == "GREEN")  emoji = "✅";       // ✅
    else if (s == "YELLOW") emoji = "⚠️"; // ⚠️
    else if (s == "RED")    emoji = "\U0001F6A8";   // 🚨
    else                    emoji = "❓";       // ❓
    return send(emoji + " AlgoForge Guard: " + state_name + "\n" + reason);
}

bool TelegramAlerter::alert_trade(const std::string& symbol, const std::string& direction,
                                  double lots, double entry, double sl, double tp) {
    const std::string arrow =
        (to_upper(direction) == "LONG") ? "\U0001F4C8" : "\U0001F4C9"; // 📈 / 📉
    const std::string msg =
        arrow + " Trade Placed\n" +
        "Symbol: " + symbol + "\n" +
        "Direction: " + direction + "\n" +
        "Lots: " + fmt("%.2f", lots) + "\n" +
        "Entry: " + fmt("%.5f", entry) + "  SL: " + fmt("%.5f", sl) +
        "  TP: " + fmt("%.5f", tp);
    return send(msg);
}

} // namespace algoforge::monitoring
