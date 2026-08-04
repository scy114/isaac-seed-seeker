#include "isaac_seed_seeker/local_web_app.hpp"

#ifdef _WIN32

#include "isaac_seed_seeker/builtin_profile.hpp"
#include "isaac_seed_seeker/core.hpp"
#include "../resources/resource.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace isaac_seed_seeker {
namespace {

enum class SessionState : std::uint8_t { idle, running, completed, cancelled, failed };

std::string_view state_name(SessionState state) noexcept {
    switch (state) {
        case SessionState::idle: return "idle";
        case SessionState::running: return "running";
        case SessionState::completed: return "completed";
        case SessionState::cancelled: return "cancelled";
        case SessionState::failed: return "failed";
    }
    return "failed";
}

std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(c); break;
        }
    }
    return result;
}

std::size_t find_json_value(std::string_view text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto found = text.find(needle);
    if (found == std::string_view::npos) {
        throw std::invalid_argument("missing JSON field: " + std::string(key));
    }
    auto position = text.find(':', found + needle.size());
    if (position == std::string_view::npos) {
        throw std::invalid_argument("malformed JSON field: " + std::string(key));
    }
    ++position;
    while (position < text.size() && std::string_view(" \t\r\n").find(text[position]) != std::string_view::npos) {
        ++position;
    }
    return position;
}

std::uint32_t json_u32(std::string_view text, std::string_view key) {
    const auto position = find_json_value(text, key);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data() + position, text.data() + text.size(), value);
    if (parsed.ec != std::errc() || value > 0xffffffffULL) {
        throw std::invalid_argument("invalid JSON uint32 field: " + std::string(key));
    }
    return static_cast<std::uint32_t>(value);
}

std::vector<std::int32_t> json_id_array(std::string_view text, std::string_view key) {
    auto position = find_json_value(text, key);
    if (position >= text.size() || text[position] != '[') {
        throw std::invalid_argument("JSON field must be an array: " + std::string(key));
    }
    ++position;
    std::vector<std::int32_t> values;
    while (position < text.size()) {
        while (position < text.size() && std::string_view(" \t\r\n,").find(text[position]) != std::string_view::npos) {
            ++position;
        }
        if (position < text.size() && text[position] == ']') {
            break;
        }
        std::int32_t value = 0;
        const auto parsed = std::from_chars(text.data() + position, text.data() + text.size(), value);
        if (parsed.ec != std::errc() || value <= 0) {
            throw std::invalid_argument("invalid item ID in JSON field: " + std::string(key));
        }
        values.push_back(value);
        position = static_cast<std::size_t>(parsed.ptr - text.data());
    }
    if (values.empty()) {
        throw std::invalid_argument("item ID array cannot be empty: " + std::string(key));
    }
    return values;
}

class SearchSession {
public:
    ~SearchSession() {
        cancel_.store(true, std::memory_order_relaxed);
        join_previous();
    }

    void start(ItemCriteria criteria, SearchOptions options) {
        if (state_.load(std::memory_order_acquire) == SessionState::running) {
            throw std::runtime_error("a search is already running");
        }
        join_previous();
        criteria.validate();
        cancel_.store(false, std::memory_order_relaxed);
        scanned_.store(0, std::memory_order_relaxed);
        total_.store(static_cast<std::uint64_t>(options.end) - options.start + 1U, std::memory_order_relaxed);
        match_count_.store(0, std::memory_order_relaxed);
        elapsed_millis_.store(0, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex_);
            result_ = {};
            error_.clear();
        }
        state_.store(SessionState::running, std::memory_order_release);
        try {
            worker_ = std::thread([this, criteria = std::move(criteria), options] {
                try {
                    const auto tables = builtin_j460_profile();
                    auto result = search(
                        tables,
                        criteria,
                        options,
                        [this](const SearchProgress& progress) {
                            scanned_.store(progress.scanned, std::memory_order_relaxed);
                            total_.store(progress.total, std::memory_order_relaxed);
                            match_count_.store(progress.matches, std::memory_order_relaxed);
                            elapsed_millis_.store(
                                static_cast<std::uint64_t>(progress.elapsed_seconds * 1000.0),
                                std::memory_order_relaxed
                            );
                        },
                        &cancel_
                    );
                    scanned_.store(result.scanned, std::memory_order_relaxed);
                    match_count_.store(result.matches.size(), std::memory_order_relaxed);
                    elapsed_millis_.store(
                        static_cast<std::uint64_t>(result.elapsed_seconds * 1000.0),
                        std::memory_order_relaxed
                    );
                    {
                        std::lock_guard lock(mutex_);
                        result_ = std::move(result);
                    }
                    state_.store(
                        cancel_.load(std::memory_order_relaxed) ? SessionState::cancelled : SessionState::completed,
                        std::memory_order_release
                    );
                } catch (const std::exception& error) {
                    {
                        std::lock_guard lock(mutex_);
                        error_ = error.what();
                    }
                    state_.store(SessionState::failed, std::memory_order_release);
                }
            });
        } catch (const std::exception& error) {
            {
                std::lock_guard lock(mutex_);
                error_ = error.what();
            }
            state_.store(SessionState::failed, std::memory_order_release);
            throw;
        }
    }

    void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }

    bool running() const noexcept {
        return state_.load(std::memory_order_acquire) == SessionState::running;
    }

    std::string status_json() const {
        const auto state = state_.load(std::memory_order_acquire);
        const auto total = total_.load(std::memory_order_relaxed);
        const auto scanned = scanned_.load(std::memory_order_relaxed);
        std::string error;
        {
            std::lock_guard lock(mutex_);
            error = error_;
        }
        std::ostringstream output;
        output << "{\"state\":\"" << state_name(state)
               << "\",\"scanned\":" << scanned
               << ",\"total\":" << total
               << ",\"progress\":" << (total == 0 ? 0.0 : 100.0 * static_cast<double>(scanned) / total)
               << ",\"matches\":" << match_count_.load(std::memory_order_relaxed)
               << ",\"elapsed_seconds\":" << std::fixed << std::setprecision(3)
               << elapsed_millis_.load(std::memory_order_relaxed) / 1000.0
               << ",\"error\":\"" << json_escape(error) << "\"}";
        return output.str();
    }

    std::string results_json() const {
        std::lock_guard lock(mutex_);
        std::ostringstream output;
        output << "{\"count\":" << result_.matches.size() << ",\"matches\":[";
        for (std::size_t index = 0; index < result_.matches.size(); ++index) {
            const auto& match = result_.matches[index];
            if (index != 0) output << ',';
            output << "{\"seed\":\"" << match.label
                   << "\",\"seed_u32\":" << match.seed
                   << ",\"trinket_id\":" << match.trinket_id
                   << ",\"active_id\":" << match.active_id
                   << ",\"passive_id\":" << match.passive_id << '}';
        }
        output << "]}";
        return output.str();
    }

    std::string results_text() const {
        std::lock_guard lock(mutex_);
        std::ostringstream output;
        for (const auto& match : result_.matches) {
            output << match.label << '\t' << match.active_id << '\t' << match.passive_id << '\n';
        }
        return output.str();
    }

private:
    void join_previous() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool cancel_{false};
    std::atomic<SessionState> state_{SessionState::idle};
    std::atomic<std::uint64_t> scanned_{0};
    std::atomic<std::uint64_t> total_{0};
    std::atomic<std::size_t> match_count_{0};
    std::atomic<std::uint64_t> elapsed_millis_{0};
    SearchResult result_;
    std::string error_;
};

std::string load_resource(int identifier) {
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(identifier), MAKEINTRESOURCEW(10));
    if (resource == nullptr) {
        throw std::runtime_error("embedded web resource is missing");
    }
    const auto loaded = LoadResource(module, resource);
    const auto size = SizeofResource(module, resource);
    const auto data = static_cast<const char*>(LockResource(loaded));
    if (data == nullptr) {
        throw std::runtime_error("cannot load embedded web resource");
    }
    return std::string(data, size);
}

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::string session_token;
};

std::string lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string header_value(std::string_view headers, std::string_view name) {
    const std::string needle = std::string(name) + ':';
    auto position = headers.find(needle);
    if (position == std::string_view::npos) {
        return {};
    }
    position += needle.size();
    while (position < headers.size() && (headers[position] == ' ' || headers[position] == '\t')) {
        ++position;
    }
    const auto end = headers.find("\r\n", position);
    return std::string(headers.substr(position, end == std::string_view::npos ? headers.size() - position : end - position));
}

std::string make_session_token() {
    std::random_device random;
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (int index = 0; index < 4; ++index) {
        output << std::setw(8) << static_cast<std::uint32_t>(random());
    }
    return output.str();
}

Request receive_request(SOCKET client) {
    std::string data;
    char buffer[8192];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const auto received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("client disconnected before sending headers");
        }
        data.append(buffer, static_cast<std::size_t>(received));
        if (data.size() > 65'536) {
            throw std::runtime_error("HTTP request is too large");
        }
        header_end = data.find("\r\n\r\n");
    }
    const auto first_line_end = data.find("\r\n");
    const auto first_space = data.find(' ');
    const auto second_space = data.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos || second_space > first_line_end) {
        throw std::runtime_error("malformed HTTP request line");
    }
    Request request;
    request.method = data.substr(0, first_space);
    request.path = data.substr(first_space + 1, second_space - first_space - 1);

    std::size_t content_length = 0;
    const auto headers_lower = lower_ascii(data.substr(first_line_end + 2, header_end - first_line_end - 2));
    request.session_token = header_value(headers_lower, "x-isaac-token");
    const auto length_key = headers_lower.find("content-length:");
    if (length_key != std::string::npos) {
        auto value_start = length_key + 15;
        while (value_start < headers_lower.size() && headers_lower[value_start] == ' ') ++value_start;
        const auto parsed = std::from_chars(
            headers_lower.data() + value_start,
            headers_lower.data() + headers_lower.size(),
            content_length
        );
        if (parsed.ec != std::errc()) {
            throw std::runtime_error("invalid Content-Length");
        }
    }
    if (content_length > 65'536) {
        throw std::runtime_error("HTTP request body is too large");
    }
    const auto body_start = header_end + 4;
    while (data.size() - body_start < content_length) {
        const auto received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("client disconnected before sending body");
        }
        data.append(buffer, static_cast<std::size_t>(received));
    }
    request.body = data.substr(body_start, content_length);
    return request;
}

void send_all(SOCKET client, std::string_view data) {
    while (!data.empty()) {
        const auto sent = send(client, data.data(), static_cast<int>(std::min<std::size_t>(data.size(), INT_MAX)), 0);
        if (sent <= 0) {
            return;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

void respond(
    SOCKET client,
    int status,
    std::string_view status_text,
    std::string_view content_type,
    std::string body,
    std::string_view extra_headers = {}
) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n"
            << extra_headers << "\r\n";
    const auto header_text = headers.str();
    send_all(client, header_text);
    send_all(client, body);
}

std::string profile_json() {
    const auto& profile = builtin_j460_profile_info();
    std::ostringstream output;
    output << "{\"id\":\"" << profile.id
           << "\",\"game_version\":\"" << profile.game_version
           << "\",\"game_build\":\"" << profile.game_build
           << "\",\"proc_source_sha256\":\"" << profile.source_collectible_table_sha256
           << "\",\"trinket_pool_source_sha256\":\"" << profile.source_trinket_pool_sha256
           << "\",\"proc_semantic_sha256\":\"" << profile.collectible_semantic_sha256
           << "\",\"trinket_pool_semantic_sha256\":\"" << profile.trinket_semantic_sha256 << "\"}";
    return output.str();
}

}  // namespace

int run_local_web_app(bool open_browser) {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
    const SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("cannot create local HTTP socket");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
        || listen(server, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        throw std::runtime_error("cannot bind local HTTP server");
    }
    int address_size = sizeof(address);
    getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size);
    const auto port = ntohs(address.sin_port);
    const std::string base_url = "http://127.0.0.1:" + std::to_string(port) + '/';
    const std::string session_token = make_session_token();
    const std::string url = base_url + "?token=" + session_token;

    const auto index_html = load_resource(IDR_WEB_INDEX);
    const auto style_css = load_resource(IDR_WEB_STYLE);
    const auto app_js = load_resource(IDR_WEB_APP);
    SearchSession session;
    std::cout << "Isaac Seed Seeker: " << url << std::endl;
    if (open_browser) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    bool keep_running = true;
    while (keep_running) {
        const SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        const DWORD socket_timeout_ms = 5'000;
        setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&socket_timeout_ms),
            sizeof(socket_timeout_ms)
        );
        setsockopt(
            client,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<const char*>(&socket_timeout_ms),
            sizeof(socket_timeout_ms)
        );
        try {
            const auto request = receive_request(client);
            if (request.method == "POST" && request.session_token != session_token) {
                respond(client, 403, "Forbidden", "application/json; charset=utf-8", "{\"error\":\"invalid session token\"}");
            } else if (request.method == "GET" && (request.path == "/" || request.path.starts_with("/?"))) {
                respond(client, 200, "OK", "text/html; charset=utf-8", index_html);
            } else if (request.method == "GET" && request.path == "/style.css") {
                respond(client, 200, "OK", "text/css; charset=utf-8", style_css);
            } else if (request.method == "GET" && request.path == "/app.js") {
                respond(client, 200, "OK", "text/javascript; charset=utf-8", app_js);
            } else if (request.method == "GET" && request.path == "/api/v1/profile") {
                respond(client, 200, "OK", "application/json; charset=utf-8", profile_json());
            } else if (request.method == "GET" && request.path == "/api/v1/search/status") {
                respond(client, 200, "OK", "application/json; charset=utf-8", session.status_json());
            } else if (request.method == "GET" && request.path == "/api/v1/search/results") {
                respond(client, 200, "OK", "application/json; charset=utf-8", session.results_json());
            } else if (request.method == "GET" && request.path == "/api/v1/search/results.txt") {
                respond(
                    client,
                    200,
                    "OK",
                    "text/plain; charset=utf-8",
                    session.results_text(),
                    "Content-Disposition: attachment; filename=isaac-seeds.txt\r\n"
                );
            } else if (request.method == "POST" && request.path == "/api/v1/search") {
                ItemCriteria criteria;
                criteria.trinket_id = static_cast<std::int32_t>(json_u32(request.body, "trinket_id"));
                criteria.active_any = json_id_array(request.body, "active_ids");
                criteria.passive_any = json_id_array(request.body, "passive_ids");
                SearchOptions options;
                options.start = json_u32(request.body, "start");
                options.end = json_u32(request.body, "end");
                options.threads = std::min(64U, json_u32(request.body, "threads"));
                options.block_size = 1'000'000;
                session.start(std::move(criteria), options);
                respond(client, 202, "Accepted", "application/json; charset=utf-8", session.status_json());
            } else if (request.method == "POST" && request.path == "/api/v1/search/cancel") {
                session.cancel();
                respond(client, 202, "Accepted", "application/json; charset=utf-8", session.status_json());
            } else if (request.method == "POST" && request.path == "/api/v1/shutdown") {
                session.cancel();
                respond(client, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
                keep_running = false;
            } else {
                respond(client, 404, "Not Found", "application/json; charset=utf-8", "{\"error\":\"not found\"}");
            }
        } catch (const std::exception& error) {
            respond(
                client,
                400,
                "Bad Request",
                "application/json; charset=utf-8",
                "{\"error\":\"" + json_escape(error.what()) + "\"}"
            );
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
    closesocket(server);
    WSACleanup();
    return 0;
}

}  // namespace isaac_seed_seeker

#else

#include <stdexcept>

namespace isaac_seed_seeker {

int run_local_web_app(bool) {
    throw std::runtime_error("the local WebUI launcher currently supports Windows only");
}

}  // namespace isaac_seed_seeker

#endif
