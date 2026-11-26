// Small websocket client on Windows' built-in WinHTTP (secure wss:// included, no extra libraries).
// Runs on its own thread; the callbacks are called from that thread, so hand work to the game's main
// thread (geode::queueInMainThread) before touching anything in the game.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class WsClient {
public:
    std::function<void(const std::string& url)> onOpen;
    std::function<void(std::string text)> onMessage;
    std::function<void(const std::string& reason)> onClose;     // also called when connecting fails
    std::function<void(const std::string& line)> onLog;

    ~WsClient();
    void connect(const std::string& address);   // "host:port", "wss://host:port" or "ws://host:port"
    bool send(const std::string& text);         // thread-safe; false when not connected
    void close();
    bool isOpen() const { return m_open; }

private:
    void run(std::string address);
    void closeHandles();

    std::thread m_thread;
    std::mutex m_mutex;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_open{false};
    void* m_session = nullptr;
    void* m_connection = nullptr;
    void* m_socket = nullptr;
};
