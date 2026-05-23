/**
 * AlgoForge — src/llm/ollama.cpp
 * S2 LLM Layer — OllamaProvider + libcurl-backed transport.
 *
 * If AF_HAS_LIBCURL is defined at compile time, CurlTransport is available
 * and used by default. Otherwise, the transport returns an
 * LLMError(unreachable) so tests (which inject MockTransport) still pass.
 *
 * Environment variables read by from_env():
 *   AF_LLM_HOST         default: http://localhost:11434
 *   AF_LLM_MODEL        default: llama3.1:8b
 *   AF_LLM_EMBED_MODEL  default: nomic-embed-text
 *   AF_LLM_TIMEOUT      default: 60
 *   AF_LLM_SEED         default: 42
 *   AF_LLM_TEMPERATURE  default: 0.2
 *   AF_LLM_MAX_TOKENS   default: 512
 */

#include "llm_internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef AF_HAS_LIBCURL
#  include <curl/curl.h>
#endif

namespace algoforge::llm {

/* =========================================================================
 * Curl transport (only compiled when libcurl is available)
 * ========================================================================= */

#ifdef AF_HAS_LIBCURL

namespace {

struct WriteBuffer {
    std::string data;
    static size_t callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* buf = static_cast<WriteBuffer*>(userdata);
        buf->data.append(ptr, size * nmemb);
        return size * nmemb;
    }
};

class CurlTransport : public ITransport {
public:
    explicit CurlTransport(std::string base_url, long timeout_s = 60)
        : base_url_(std::move(base_url)), timeout_s_(timeout_s) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlTransport() override {
        curl_global_cleanup();
    }

    TransportResult request(const std::string& method,
                            const std::string& path,
                            const std::string& body) override {
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw LLMError(LLMErrorKind::unreachable, "curl_easy_init failed");
        }

        std::string url = base_url_ + path;
        WriteBuffer buf;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s_);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBuffer::callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(body.size()));
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OPERATION_TIMEDOUT) {
            throw LLMError(LLMErrorKind::timeout, "request timed out");
        }
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST) {
            throw LLMError(LLMErrorKind::unreachable,
                           std::string("unreachable: ") + curl_easy_strerror(res));
        }
        if (res != CURLE_OK) {
            throw LLMError(LLMErrorKind::unreachable,
                           std::string("curl error: ") + curl_easy_strerror(res));
        }

        TransportResult tr;
        tr.status = static_cast<int>(http_code);
        tr.body   = std::move(buf.data);
        tr.ok     = (http_code >= 200 && http_code < 300);
        return tr;
    }

private:
    std::string base_url_;
    long        timeout_s_;
};

} /* anon namespace */

#endif /* AF_HAS_LIBCURL */

/* =========================================================================
 * Fallback transport (no libcurl)
 * ========================================================================= */

namespace {

class NoopTransport : public ITransport {
public:
    TransportResult request(const std::string& /*method*/,
                            const std::string& /*path*/,
                            const std::string& /*body*/) override {
        throw LLMError(LLMErrorKind::unreachable,
                       "libcurl not available; cannot make HTTP requests");
    }
};

} /* anon namespace */

/* =========================================================================
 * JSON builder helpers for Ollama wire format
 * ========================================================================= */

static std::string build_completion_body(const CompletionRequest& req) {
    /* Build options object */
    std::map<std::string, std::string> opts;
    int num_predict = req.max_tokens.value_or(512);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", num_predict);
    opts["num_predict"] = buf;
    if (req.seed.has_value()) {
        snprintf(buf, sizeof(buf), "%d", req.seed.value());
        opts["seed"] = buf;
    }
    snprintf(buf, sizeof(buf), "%.17g", static_cast<double>(req.temperature));
    opts["temperature"] = buf;

    /* Build stop array if needed */
    std::string stop_json;
    if (!req.stop.empty()) {
        stop_json = "[";
        for (size_t i = 0; i < req.stop.size(); ++i) {
            if (i > 0) stop_json += ",";
            stop_json += json::emit_string_val(req.stop[i]);
        }
        stop_json += "]";
        opts["stop"] = stop_json;
    }

    std::string options_str = json::emit_object(opts);

    /* Outer object */
    std::map<std::string, std::string> outer;
    outer["model"]   = json::emit_string_val(req.model);
    outer["prompt"]  = json::emit_string_val(req.prompt);
    outer["stream"]  = json::emit_bool(false);
    outer["options"] = options_str;

    return json::emit_object(outer);
}

static std::string build_chat_body(const ChatRequest& req, bool stream = false) {
    /* Build messages array */
    std::string msgs = "[";
    for (size_t i = 0; i < req.messages.size(); ++i) {
        if (i > 0) msgs += ",";
        std::map<std::string, std::string> m;
        m["content"] = json::emit_string_val(req.messages[i].content);
        m["role"]    = json::emit_string_val(req.messages[i].role);
        msgs += json::emit_object(m);
    }
    msgs += "]";

    /* Build options object */
    std::map<std::string, std::string> opts;
    int num_predict = req.max_tokens.value_or(512);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", num_predict);
    opts["num_predict"] = buf;
    if (req.seed.has_value()) {
        snprintf(buf, sizeof(buf), "%d", req.seed.value());
        opts["seed"] = buf;
    }
    snprintf(buf, sizeof(buf), "%.17g", static_cast<double>(req.temperature));
    opts["temperature"] = buf;

    std::string options_str = json::emit_object(opts);

    std::map<std::string, std::string> outer;
    outer["messages"] = msgs;
    outer["model"]    = json::emit_string_val(req.model);
    outer["options"]  = options_str;
    outer["stream"]   = json::emit_bool(stream);

    return json::emit_object(outer);
}

static std::string build_embed_body(const std::string& model, const std::string& prompt) {
    std::map<std::string, std::string> outer;
    outer["model"]  = json::emit_string_val(model);
    outer["prompt"] = json::emit_string_val(prompt);
    return json::emit_object(outer);
}

/* =========================================================================
 * Response parsers
 * ========================================================================= */

static CompletionResponse parse_completion_response(const std::string& body) {
    json::JsonValue jv;
    try { jv = json::parse(body); }
    catch (const std::exception& e) {
        throw LLMError(LLMErrorKind::decode,
                       std::string("completion parse error: ") + e.what());
    }
    if (jv.type != json::JsonType::Object) {
        throw LLMError(LLMErrorKind::decode, "completion: response is not an object");
    }

    CompletionResponse r;
    if (jv.has("response"))           r.text               = jv.get("response").as_str();
    if (jv.has("model"))              r.model              = jv.get("model").as_str();
    if (jv.has("done_reason"))        r.finish_reason      = jv.get("done_reason").as_str();
    if (jv.has("prompt_eval_count"))  r.prompt_tokens      = static_cast<int>(jv.get("prompt_eval_count").as_int());
    if (jv.has("eval_count"))         r.completion_tokens  = static_cast<int>(jv.get("eval_count").as_int());
    if (jv.has("total_duration"))     r.total_duration_ns  = jv.get("total_duration").as_int();
    return r;
}

static ChatResponse parse_chat_response(const std::string& body) {
    json::JsonValue jv;
    try { jv = json::parse(body); }
    catch (const std::exception& e) {
        throw LLMError(LLMErrorKind::decode,
                       std::string("chat parse error: ") + e.what());
    }
    if (jv.type != json::JsonType::Object) {
        throw LLMError(LLMErrorKind::decode, "chat: response is not an object");
    }

    ChatResponse r;
    if (jv.has("model"))              r.model              = jv.get("model").as_str();
    if (jv.has("done_reason"))        r.finish_reason      = jv.get("done_reason").as_str();
    if (jv.has("prompt_eval_count"))  r.prompt_tokens      = static_cast<int>(jv.get("prompt_eval_count").as_int());
    if (jv.has("eval_count"))         r.completion_tokens  = static_cast<int>(jv.get("eval_count").as_int());
    if (jv.has("total_duration"))     r.total_duration_ns  = jv.get("total_duration").as_int();
    if (jv.has("message")) {
        const auto& msg = jv.get("message");
        if (msg.has("role"))    r.message.role    = msg.get("role").as_str();
        if (msg.has("content")) r.message.content = msg.get("content").as_str();
    }
    return r;
}

static std::vector<ModelInfo> parse_tags_response(const std::string& body) {
    json::JsonValue jv;
    try { jv = json::parse(body); }
    catch (const std::exception& e) {
        throw LLMError(LLMErrorKind::decode,
                       std::string("list_models parse error: ") + e.what());
    }

    std::vector<ModelInfo> result;
    if (!jv.has("models")) return result;
    const auto& arr = jv.get("models");
    if (arr.type != json::JsonType::Array) return result;
    for (const auto& item : arr.arr) {
        ModelInfo mi;
        if (item.has("name"))        mi.name        = item.get("name").as_str();
        if (item.has("size"))        mi.size_bytes  = item.get("size").as_int();
        if (item.has("modified_at")) mi.modified_at = item.get("modified_at").as_str();
        result.push_back(std::move(mi));
    }
    return result;
}

/* =========================================================================
 * OllamaProvider implementation
 * ========================================================================= */

OllamaProvider::OllamaProvider(std::string            base_url,
                                std::string            default_model,
                                std::string            embed_model,
                                std::shared_ptr<ITransport> transport)
    : base_url_(std::move(base_url))
    , default_model_(std::move(default_model))
    , embed_model_(std::move(embed_model))
    , transport_(std::move(transport))
{
    if (!transport_) {
#ifdef AF_HAS_LIBCURL
        transport_ = std::make_shared<CurlTransport>(base_url_);
#else
        transport_ = std::make_shared<NoopTransport>();
#endif
    }
}

/* Read env variable, return default if not set */
static std::string env_or(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return v ? v : def;
}
static int env_int_or(const char* name, int def) {
    const char* v = std::getenv(name);
    return v ? std::atoi(v) : def;
}
static float env_float_or(const char* name, float def) {
    const char* v = std::getenv(name);
    return v ? static_cast<float>(std::atof(v)) : def;
}

OllamaProvider OllamaProvider::from_env() {
    std::string host  = env_or("AF_LLM_HOST",        "http://localhost:11434");
    std::string model = env_or("AF_LLM_MODEL",        "llama3.1:8b");
    std::string embed = env_or("AF_LLM_EMBED_MODEL",  "nomic-embed-text");
    return OllamaProvider(host, model, embed);
}

TransportResult OllamaProvider::do_request(const std::string& method,
                                             const std::string& path,
                                             const std::string& body) {
    return transport_->request(method, path, body);
}

HealthStatus OllamaProvider::health() {
    HealthStatus hs;
    /* Step 1: GET / — check reachability */
    try {
        TransportResult r = do_request("GET", "/", "");
        hs.ok = r.ok;
    } catch (const LLMError& e) {
        hs.ok    = false;
        hs.error = e.what();
        return hs;
    }
    if (!hs.ok) {
        hs.error = "Ollama root endpoint returned non-200";
        return hs;
    }

    /* Step 2: GET /api/tags — list models */
    try {
        TransportResult r = do_request("GET", "/api/tags", "");
        if (!r.ok) {
            hs.ok    = false;
            hs.error = "list_models returned HTTP " + std::to_string(r.status);
            return hs;
        }
        auto models = parse_tags_response(r.body);
        hs.ok = true;
        /* Step 3: check if default model is loaded */
        for (const auto& m : models) {
            if (m.name == default_model_) {
                hs.model_loaded = true;
                hs.model        = m.name;
                break;
            }
        }
        if (!hs.model.has_value() && !models.empty()) {
            hs.model = models.front().name;
        }
    } catch (const LLMError& e) {
        hs.ok    = false;
        hs.error = e.what();
    }
    return hs;
}

std::vector<ModelInfo> OllamaProvider::list_models() {
    TransportResult r;
    try {
        r = do_request("GET", "/api/tags", "");
    } catch (const LLMError& e) {
        throw;
    }
    if (!r.ok) {
        throw LLMError(LLMErrorKind::http,
                       "list_models HTTP " + std::to_string(r.status), r.status);
    }
    return parse_tags_response(r.body);
}

CompletionResponse OllamaProvider::complete(const CompletionRequest& req) {
    std::string body = build_completion_body(req);
    TransportResult r;
    try {
        r = do_request("POST", "/api/generate", body);
    } catch (const LLMError&) {
        throw;
    }
    if (!r.ok) {
        if (r.status == 404) {
            throw LLMError(LLMErrorKind::model_missing,
                           "model not found: " + req.model, r.status);
        }
        throw LLMError(LLMErrorKind::http,
                       "complete HTTP " + std::to_string(r.status), r.status);
    }
    return parse_completion_response(r.body);
}

ChatResponse OllamaProvider::chat(const ChatRequest& req) {
    std::string body = build_chat_body(req, false);
    TransportResult r;
    try {
        r = do_request("POST", "/api/chat", body);
    } catch (const LLMError&) {
        throw;
    }
    if (!r.ok) {
        if (r.status == 404) {
            throw LLMError(LLMErrorKind::model_missing,
                           "model not found: " + req.model, r.status);
        }
        throw LLMError(LLMErrorKind::http,
                       "chat HTTP " + std::to_string(r.status), r.status);
    }
    return parse_chat_response(r.body);
}

std::vector<ChatChunk> OllamaProvider::chat_stream(const ChatRequest& req) {
    /* For the test harness, the streaming fixture returns an array of chunk
     * objects rather than real NDJSON.  The mock transport will return the
     * full array body; we parse it here as an array. */
    std::string body_req = build_chat_body(req, true);
    TransportResult r;
    try {
        r = do_request("POST", "/api/chat", body_req);
    } catch (const LLMError&) {
        throw;
    }
    if (!r.ok) {
        throw LLMError(LLMErrorKind::http,
                       "chat_stream HTTP " + std::to_string(r.status), r.status);
    }

    std::vector<ChatChunk> chunks;
    /* Try to parse as JSON array (mock) or NDJSON lines (live) */
    const std::string& text = r.body;
    /* Trim leading whitespace to detect array vs NDJSON */
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' ||
                                    text[start] == '\n' || text[start] == '\r'))
        ++start;

    auto parse_chunk = [&](const std::string& line) {
        json::JsonValue jv;
        try { jv = json::parse(line); }
        catch (...) { return; }
        if (jv.type != json::JsonType::Object) return;

        ChatChunk chunk;
        if (jv.has("done"))  chunk.done = jv.get("done").as_bool();
        if (jv.has("done_reason")) {
            const auto& dr = jv.get("done_reason");
            if (dr.type == json::JsonType::String)
                chunk.finish_reason = dr.as_str();
        }
        if (jv.has("message")) {
            const auto& msg = jv.get("message");
            if (msg.has("content")) chunk.delta = msg.get("content").as_str();
        }
        chunks.push_back(std::move(chunk));
    };

    if (start < text.size() && text[start] == '[') {
        /* JSON array from mock transport */
        json::JsonValue arr;
        try { arr = json::parse(text); }
        catch (...) {
            throw LLMError(LLMErrorKind::decode, "chat_stream: invalid JSON array");
        }
        if (arr.type == json::JsonType::Array) {
            for (const auto& item : arr.arr) {
                ChatChunk chunk;
                if (item.has("done"))  chunk.done = item.get("done").as_bool();
                if (item.has("done_reason")) {
                    const auto& dr = item.get("done_reason");
                    if (dr.type == json::JsonType::String)
                        chunk.finish_reason = dr.as_str();
                }
                if (item.has("message")) {
                    const auto& msg = item.get("message");
                    if (msg.has("content")) chunk.delta = msg.get("content").as_str();
                }
                chunks.push_back(std::move(chunk));
            }
        }
    } else {
        /* NDJSON lines */
        std::istringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) parse_chunk(line);
        }
    }
    return chunks;
}

EmbedResponse OllamaProvider::embed(const EmbedRequest& req) {
    EmbedResponse result;
    result.model = req.model.empty() ? embed_model_ : req.model;

    for (const auto& text : req.input) {
        std::string body = build_embed_body(result.model, text);
        TransportResult r;
        try {
            r = do_request("POST", "/api/embeddings", body);
        } catch (const LLMError&) {
            throw;
        }
        if (!r.ok) {
            throw LLMError(LLMErrorKind::http,
                           "embed HTTP " + std::to_string(r.status), r.status);
        }

        json::JsonValue jv;
        try { jv = json::parse(r.body); }
        catch (const std::exception& e) {
            throw LLMError(LLMErrorKind::decode,
                           std::string("embed parse error: ") + e.what());
        }

        std::vector<float> emb;
        if (jv.has("embedding")) {
            const auto& arr = jv.get("embedding");
            if (arr.type == json::JsonType::Array) {
                for (const auto& v : arr.arr) {
                    emb.push_back(static_cast<float>(v.as_double()));
                }
            }
        }
        result.embeddings.push_back(std::move(emb));
    }
    return result;
}

} /* namespace algoforge::llm */
