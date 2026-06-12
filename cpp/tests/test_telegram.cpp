/**
 * AlgoForge — tests/test_telegram.cpp
 * Parity tests for monitoring::TelegramAlerter. Python oracle: python/algoforge/telegram.py.
 *
 * The HTTP POST is injected with a capturing sender, so the message-formatting logic
 * (alert / alert_guard_state / alert_trade / build_payload + the enabled/disabled gate)
 * is verified without any network. Standalone runner (own main).
 */
#include "test_helpers.hpp"
#include "monitoring/telegram.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <utility>

using algoforge::monitoring::TelegramAlerter;

static int g_pass = 0, g_fail = 0;
static std::vector<std::pair<std::string, std::string>> g_failures;

void run_test(const char* name, std::function<void()> fn) {
    try {
        fn();
        printf("  \033[92m✓\033[0m %-66s\n", name); g_pass++;
    } catch (std::string& e) {
        printf("  \033[91m✗\033[0m %-66s\n       → %s\n", name, e.c_str());
        g_fail++; g_failures.push_back({name, e});
    } catch (std::exception& ex) {
        printf("  \033[91m✗\033[0m %-66s EXCEPTION\n       → %s\n", name, ex.what());
        g_fail++; g_failures.push_back({name, ex.what()});
    }
}
void section(const char* title) { printf("\n  %s\n  %s\n", title, std::string(72, '-').c_str()); }

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A capturing sender: records the last (url, body) and reports configurable success.
struct Capture {
    std::string url, body;
    int         calls = 0;
    bool        result = true;
    TelegramAlerter::HttpPost fn() {
        return [this](const std::string& u, const std::string& b) {
            url = u; body = b; ++calls; return result;
        };
    }
};

void run_telegram_tests() {
    section("telegram — enabled gate (token + chat_id required)");
    run_test("disabled with no creds → send() returns false, no HTTP call", [] {
        Capture cap;
        TelegramAlerter tg("", "", cap.fn());
        CHK_FALSE(tg.enabled());
        CHK_FALSE(tg.send("hi"));
        CHK_EQ(cap.calls, 0);
    });
    run_test("disabled with only token (no chat_id)", [] {
        TelegramAlerter tg("tok", "");
        CHK_FALSE(tg.enabled());
    });
    run_test("enabled with token + chat_id", [] {
        TelegramAlerter tg("tok", "123");
        CHK(tg.enabled());
    });

    section("telegram — send_url + build_payload");
    run_test("send_url substitutes {token}", [] {
        TelegramAlerter tg("ABC", "123");
        CHK_EQ(tg.send_url(), std::string("https://api.telegram.org/botABC/sendMessage"));
    });
    run_test("build_payload emits chat_id + text; omits empty parse_mode", [] {
        auto p = TelegramAlerter::build_payload("123", "hello", "");
        CHK_EQ(p, std::string("{\"chat_id\":\"123\",\"text\":\"hello\"}"));
        auto p2 = TelegramAlerter::build_payload("123", "hi", "HTML");
        CHK(contains(p2, "\"parse_mode\":\"HTML\""));
    });
    run_test("build_payload escapes control chars + quotes", [] {
        auto p = TelegramAlerter::build_payload("c", "a\"b\nc", "");
        CHK(contains(p, "a\\\"b\\nc"));
    });

    section("telegram — send posts to the resolved url + payload");
    run_test("send() calls sender with url + JSON body, returns its result", [] {
        Capture cap;
        TelegramAlerter tg("ABC", "123", cap.fn());
        CHK(tg.send("ping"));
        CHK_EQ(cap.calls, 1);
        CHK_EQ(cap.url, std::string("https://api.telegram.org/botABC/sendMessage"));
        CHK(contains(cap.body, "\"text\":\"ping\""));
        CHK(contains(cap.body, "\"chat_id\":\"123\""));
    });
    run_test("send() propagates sender failure", [] {
        Capture cap; cap.result = false;
        TelegramAlerter tg("ABC", "123", cap.fn());
        CHK_FALSE(tg.send("ping"));
    });

    section("telegram — alert formatting parity");
    run_test("alert: '[LEVEL] msg' with level uppercased", [] {
        Capture cap;
        TelegramAlerter tg("t", "c", cap.fn());
        tg.alert("critical", "Trading halted");
        CHK(contains(cap.body, "[CRITICAL] Trading halted"));
    });
    run_test("alert_guard_state: emoji + 'AlgoForge Guard: STATE\\nreason'", [] {
        Capture cap;
        TelegramAlerter tg("t", "c", cap.fn());
        tg.alert_guard_state("RED", "Daily loss limit reached");
        CHK(contains(cap.body, "AlgoForge Guard: RED"));
        CHK(contains(cap.body, "Daily loss limit reached"));
        CHK(contains(cap.body, "\U0001F6A8"));     // 🚨
        Capture cap2;
        TelegramAlerter tg2("t", "c", cap2.fn());
        tg2.alert_guard_state("green", "ok");
        CHK(contains(cap2.body, "✅"));         // ✅ (state matched case-insensitively)
    });
    run_test("alert_trade: arrow + lots %.2f + prices %.5f", [] {
        Capture cap;
        TelegramAlerter tg("t", "c", cap.fn());
        tg.alert_trade("EURUSD", "LONG", 0.10, 1.08500, 1.08200, 1.09100);
        CHK(contains(cap.body, "\U0001F4C8"));      // 📈 (LONG)
        CHK(contains(cap.body, "Symbol: EURUSD"));
        CHK(contains(cap.body, "Direction: LONG"));
        CHK(contains(cap.body, "Lots: 0.10"));
        CHK(contains(cap.body, "Entry: 1.08500"));
        CHK(contains(cap.body, "SL: 1.08200"));
        CHK(contains(cap.body, "TP: 1.09100"));
        Capture cap2;
        TelegramAlerter tg2("t", "c", cap2.fn());
        tg2.alert_trade("USDJPY", "short", 1.0, 150.0, 151.0, 149.0);
        CHK(contains(cap2.body, "\U0001F4C9"));     // 📉 (SHORT)
    });
}

int main() {
    printf("\n  %s\n", std::string(72, '=').c_str());
    printf("  AlgoForge TelegramAlerter Parity Tests\n");
    printf("  %s\n", std::string(72, '=').c_str());
    run_telegram_tests();
    int total = g_pass + g_fail;
    printf("\n  %s\n", std::string(72, '=').c_str());
    printf("  COMPLETE | Passed: %d | Failed: %d | Total: %d | Score: %.1f%%\n",
           g_pass, g_fail, total, total ? (double)g_pass / total * 100 : 0.0);
    if (!g_failures.empty()) {
        printf("\n  FAILURES:\n");
        for (auto& [n, e] : g_failures) printf("    ✗ %s\n      → %s\n", n.c_str(), e.c_str());
    }
    printf("  %s\n\n", std::string(72, '=').c_str());
    return g_fail == 0 ? 0 : 1;
}
