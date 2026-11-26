#include "ws_client.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string errorText(const char* step) {
    DWORD code = GetLastError();
    std::string hint;
    switch (code) {
        case ERROR_WINHTTP_CANNOT_CONNECT: hint = " (nothing answering on that port: is the room running? the port may have changed)"; break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: hint = " (server name not found: check the spelling)"; break;
        case ERROR_WINHTTP_TIMEOUT: hint = " (timed out)"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: hint = " (secure connection failed)"; break;
        default: break;
    }
    return std::string(step) + " failed, error " + std::to_string(code) + hint;
}

struct Target {
    bool secure;
    std::string host;
    INTERNET_PORT port;
};

}  // namespace

WsClient::~WsClient() { close(); }

void WsClient::connect(const std::string& address) {
    close();
    m_stop = false;
    m_thread = std::thread([this, address] { run(address); });
}

void WsClient::closeHandles() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket) { WinHttpCloseHandle(static_cast<HINTERNET>(m_socket)); m_socket = nullptr; }
    if (m_connection) { WinHttpCloseHandle(static_cast<HINTERNET>(m_connection)); m_connection = nullptr; }
    if (m_session) { WinHttpCloseHandle(static_cast<HINTERNET>(m_session)); m_session = nullptr; }
    m_open = false;
}

void WsClient::close() {
    m_stop = true;
    closeHandles();                                  // closing the handles wakes a blocked receive
    if (m_thread.joinable()) {
        if (m_thread.get_id() == std::this_thread::get_id()) m_thread.detach();
        else m_thread.join();
    }
}

bool WsClient::send(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_socket || !m_open) return false;
    DWORD rc = WinHttpWebSocketSend(static_cast<HINTERNET>(m_socket), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                    const_cast<char*>(text.data()), static_cast<DWORD>(text.size()));
    return rc == NO_ERROR;
}

void WsClient::run(std::string address) {
    auto log = [this](const std::string& s) { if (onLog) onLog(s); };
    std::vector<Target> targets;
    std::string rest = address;
    int scheme = 0;                                   // 0 = try wss then ws, 1 = wss only, 2 = ws only
    if (rest.rfind("wss://", 0) == 0) { scheme = 1; rest = rest.substr(6); }
    else if (rest.rfind("ws://", 0) == 0) { scheme = 2; rest = rest.substr(5); }
    while (!rest.empty() && (rest.back() == '/' || rest.back() == ' ')) rest.pop_back();
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    std::string host = rest;
    INTERNET_PORT port = 38281;
    auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        try { port = static_cast<INTERNET_PORT>(std::stoi(rest.substr(colon + 1))); } catch (...) {}
    }
    if (host.empty()) {
        if (onClose) onClose("no server address set (Geode > Mods > Geometry Dash Archipelago > settings)");
        return;
    }
    if (scheme != 2) targets.push_back({true, host, port});
    if (scheme != 1) targets.push_back({false, host, port});

    std::string lastError = "could not connect";
    for (auto& t : targets) {
        if (m_stop) return;
        std::string url = std::string(t.secure ? "wss://" : "ws://") + t.host + ":" + std::to_string(t.port);
        HINTERNET session = WinHttpOpen(L"GeometryDashArchipelago/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)   // older Windows without automatic proxy support
            session = WinHttpOpen(L"GeometryDashArchipelago/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { lastError = errorText("WinHttpOpen"); continue; }
        WinHttpSetTimeouts(session, 10000, 10000, 10000, 15000);
        HINTERNET connection = WinHttpConnect(session, widen(t.host).c_str(), t.port, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                            t.secure ? WINHTTP_FLAG_SECURE : 0)
                                       : nullptr;
        bool ok = request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
                  WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                  WinHttpReceiveResponse(request, nullptr);
        if (!ok) lastError = errorText(connection ? "Connecting" : "WinHttpConnect");
        DWORD status = 0, size = sizeof(status);
        if (ok) {
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status != 101) { ok = false; lastError = "server answered HTTP " + std::to_string(status) + " instead of a websocket"; }
        }
        HINTERNET socket = ok ? WinHttpWebSocketCompleteUpgrade(request, 0) : nullptr;
        if (ok && !socket) lastError = errorText("Websocket upgrade");
        if (request) WinHttpCloseHandle(request);
        if (!socket) {
            log(url + ": " + lastError);
            if (connection) WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            continue;
        }
        // Receives wait as long as needed; Archipelago rooms can be quiet for a long time.
        WinHttpSetTimeouts(session, 10000, 10000, 10000, 0);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_session = session;
            m_connection = connection;
            m_socket = socket;
            m_open = true;
        }
        if (m_stop) { closeHandles(); return; }
        if (onOpen) onOpen(url);

        std::string message;
        std::vector<char> buffer(1 << 16);
        std::string reason = "connection closed";
        while (!m_stop) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            HINTERNET h;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                h = static_cast<HINTERNET>(m_socket);
            }
            if (!h) break;
            DWORD rc = WinHttpWebSocketReceive(h, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
            if (rc != NO_ERROR) { reason = "connection lost (error " + std::to_string(rc) + ")"; break; }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { reason = "server closed the connection"; break; }
            message.append(buffer.data(), read);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                if (onMessage) onMessage(std::move(message));
                message.clear();
            }
        }
        bool stopped = m_stop;
        closeHandles();
        if (!stopped && onClose) onClose(reason);
        return;
    }
    if (!m_stop && onClose) onClose(lastError);
}

#else   // not Windows: the mod is Windows-only for now; keep the class so other builds still link

WsClient::~WsClient() = default;
void WsClient::connect(const std::string&) { if (onClose) onClose("this build only supports Windows"); }
bool WsClient::send(const std::string&) { return false; }
void WsClient::close() {}
void WsClient::run(std::string) {}
void WsClient::closeHandles() {}

#endif
