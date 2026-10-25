#pragma once

#include <switch.h>
#include <string>
#include <atomic>
#include <vector>

namespace sl::menu::net {

    class MusicServer {
    public:
        static MusicServer &Instance();

        bool Start(uint16_t port = 8080);
        void Stop();
        bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }
        uint16_t Port() const { return m_port; }
        int UploadCount() const { return m_upload_count.load(std::memory_order_relaxed); }
        std::string LastUploadedFile() const;
        void ResetSession();

    private:
        MusicServer() = default;
        ~MusicServer() { Stop(); }

        static void ServerThreadTrampoline(void *arg);
        void Run();
        void HandleClient(int client_fd);

        Thread m_thread{};
        bool m_thread_valid = false;
        std::atomic<bool> m_running{false};
        std::atomic<bool> m_stop_requested{false};
        uint16_t m_port = 8080;
        int m_server_fd = -1;

        std::atomic<int> m_upload_count{0};
        mutable std::string m_last_file;
    };

} // namespace sl::menu::net
