#include <sl/menu/net/MusicServer.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

namespace sl::menu::net {

    namespace {
        constexpr const char *kMusicDir = "sdmc:/slaunch/music";

        bool HasAudioExtension(const std::string &name) {
            size_t n = name.size();
            auto ends = [&](const char *e) {
                size_t k = strlen(e);
                return n > k && strcasecmp(name.c_str() + n - k, e) == 0;
            };
            return ends(".mp3") || ends(".ogg") || ends(".flac") ||
                   ends(".opus") || ends(".mod") || ends(".wav") || ends(".m4a");
        }

        std::string UrlDecode(const std::string &src) {
            std::string res;
            res.reserve(src.size());
            for (size_t i = 0; i < src.size(); i++) {
                if (src[i] == '%' && i + 2 < src.size()) {
                    char hex[3] = { src[i + 1], src[i + 2], 0 };
                    char *end = nullptr;
                    long val = strtol(hex, &end, 16);
                    if (end && *end == 0) {
                        res.push_back((char)val);
                        i += 2;
                        continue;
                    }
                } else if (src[i] == '+') {
                    res.push_back(' ');
                    continue;
                }
                res.push_back(src[i]);
            }
            return res;
        }

        std::string SanitizeFilename(const std::string &raw) {
            std::string s = UrlDecode(raw);
            size_t slash = s.find_last_of("/\\");
            if (slash != std::string::npos) s = s.substr(slash + 1);
            std::string clean;
            for (char c : s) {
                if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                    c == '?' || c == '"' || c == '<' || c == '>' || c == '|') continue;
                if ((unsigned char)c < 32) continue;
                clean.push_back(c);
            }
            while (clean.find("..") != std::string::npos) {
                size_t p = clean.find("..");
                clean.erase(p, 2);
            }
            if (clean.empty()) clean = "track.mp3";
            return clean;
        }

        const char kIndexHtml[] = R"rawhtml(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0">
<title>KXLaunch - Загрузка Музыки</title>
<style>
:root {
  --bg: #121318;
  --card: #1c1e24;
  --accent: #20c997;
  --accent-glow: rgba(32, 201, 151, 0.35);
  --danger: #ff4757;
  --text: #ffffff;
  --text-dim: #9aa0a6;
  --border: #2e323b;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
body { background: var(--bg); color: var(--text); padding: 16px; min-height: 100vh; display: flex; flex-direction: column; align-items: center; }
.container { width: 100%; max-width: 600px; display: flex; flex-direction: column; gap: 16px; }
.header { text-align: center; padding: 12px 0; }
.header h1 { font-size: 24px; font-weight: 700; display: flex; align-items: center; justify-content: center; gap: 8px; color: #fff; }
.header h1 span { color: var(--accent); }
.badge { display: inline-block; background: rgba(32, 201, 151, 0.15); color: var(--accent); border: 1px solid var(--accent); padding: 4px 12px; border-radius: 20px; font-size: 13px; font-weight: 600; margin-top: 8px; }
.card { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 20px; box-shadow: 0 8px 24px rgba(0,0,0,0.3); }
.drop-zone { border: 2px dashed var(--border); border-radius: 12px; padding: 28px 16px; text-align: center; cursor: pointer; transition: all 0.2s ease; background: rgba(255,255,255,0.02); }
.drop-zone:hover, .drop-zone.dragover { border-color: var(--accent); background: var(--accent-glow); }
.drop-zone svg { width: 48px; height: 48px; fill: var(--accent); margin-bottom: 12px; }
.drop-zone p { font-size: 15px; color: var(--text); margin-bottom: 6px; }
.drop-zone span { font-size: 13px; color: var(--text-dim); }
.btn { display: inline-flex; align-items: center; justify-content: center; gap: 8px; background: var(--accent); color: #000; font-weight: 600; font-size: 15px; padding: 12px 24px; border-radius: 12px; border: none; cursor: pointer; margin-top: 14px; width: 100%; transition: transform 0.1s, opacity 0.2s; }
.btn:active { transform: scale(0.98); }
.progress-card { display: none; margin-top: 14px; }
.progress-bar-bg { width: 100%; height: 10px; background: var(--border); border-radius: 5px; overflow: hidden; margin: 8px 0; }
.progress-bar { width: 0%; height: 100%; background: linear-gradient(90deg, #20c997, #00d2ff); transition: width 0.1s linear; }
.progress-info { display: flex; justify-content: space-between; font-size: 13px; color: var(--text-dim); }
.track-list-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
.track-list-header h2 { font-size: 17px; font-weight: 600; }
.btn-refresh { background: none; border: 1px solid var(--border); color: var(--text-dim); font-size: 12px; padding: 4px 10px; border-radius: 8px; cursor: pointer; }
.track-item { display: flex; align-items: center; justify-content: space-between; padding: 10px 12px; border-bottom: 1px solid var(--border); }
.track-item:last-child { border-bottom: none; }
.track-info { display: flex; flex-direction: column; overflow: hidden; }
.track-name { font-size: 14px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 320px; }
.track-size { font-size: 12px; color: var(--text-dim); margin-top: 2px; }
.btn-del { background: rgba(255, 71, 87, 0.15); border: none; color: var(--danger); width: 32px; height: 32px; border-radius: 8px; cursor: pointer; display: flex; align-items: center; justify-content: center; }
.empty-list { text-align: center; color: var(--text-dim); padding: 20px 0; font-size: 14px; }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <h1>🎵 KXLaunch <span>Music</span></h1>
    <div class="badge">● Nintendo Switch Онлайн</div>
  </div>

  <div class="card">
    <div class="drop-zone" id="dropZone" onclick="document.getElementById('fileInput').click()">
      <svg viewBox="0 0 24 24"><path d="M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM14 13v4h-4v-4H7l5-5 5 5h-3z"/></svg>
      <p>Нажмите или перетащите аудиофайлы сюда</p>
      <span>Поддерживаются: MP3, FLAC, OGG, WAV, M4A</span>
    </div>
    <input type="file" id="fileInput" multiple accept=".mp3,.flac,.ogg,.wav,.opus,.mod,.m4a" style="display:none" onchange="handleFiles(this.files)">
    <button class="btn" onclick="document.getElementById('fileInput').click()">Выбрать треки с телефона</button>

    <div class="progress-card" id="progressCard">
      <div class="progress-info">
        <span id="progressFile">Загрузка...</span>
        <span id="progressPercent">0%</span>
      </div>
      <div class="progress-bar-bg">
        <div class="progress-bar" id="progressBar"></div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="track-list-header">
      <h2>Треки на консоли (<span id="trackCount">0</span>)</h2>
      <button class="btn-refresh" onclick="loadTrackList()">Обновить</button>
    </div>
    <div id="trackList">
      <div class="empty-list">Загрузка списка...</div>
    </div>
  </div>
</div>

<script>
const dropZone = document.getElementById('dropZone');
dropZone.addEventListener('dragover', (e) => { e.preventDefault(); dropZone.classList.add('dragover'); });
dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));
dropZone.addEventListener('drop', (e) => {
  e.preventDefault();
  dropZone.classList.remove('dragover');
  if (e.dataTransfer.files.length) handleFiles(e.dataTransfer.files);
});

function formatBytes(b) {
  if (b < 1024) return b + ' B';
  if (b < 1024*1024) return (b/1024).toFixed(1) + ' KB';
  return (b/(1024*1024)).toFixed(1) + ' MB';
}

function loadTrackList() {
  fetch('/api/list')
    .then(r => r.json())
    .then(data => {
      document.getElementById('trackCount').innerText = data.length;
      const el = document.getElementById('trackList');
      if (data.length === 0) {
        el.innerHTML = '<div class="empty-list">Нет треков. Загрузите файлы выше!</div>';
        return;
      }
      let html = '';
      data.forEach(t => {
        html += `<div class="track-item">
          <div class="track-info">
            <span class="track-name">${t.name}</span>
            <span class="track-size">${formatBytes(t.size)}</span>
          </div>
          <button class="btn-del" title="Удалить" onclick="deleteTrack('${encodeURIComponent(t.name)}')">🗑</button>
        </div>`;
      });
      el.innerHTML = html;
    })
    .catch(() => {
      document.getElementById('trackList').innerHTML = '<div class="empty-list">Ошибка загрузки списка</div>';
    });
}

async function handleFiles(files) {
  if (!files || !files.length) return;
  const pCard = document.getElementById('progressCard');
  const pBar = document.getElementById('progressBar');
  const pFile = document.getElementById('progressFile');
  const pPct = document.getElementById('progressPercent');
  pCard.style.display = 'block';

  for (let i = 0; i < files.length; i++) {
    const file = files[i];
    pFile.innerText = `(${i+1}/${files.length}) ${file.name}`;
    pPct.innerText = '0%';
    pBar.style.width = '0%';

    await new Promise((resolve) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', `/api/upload?name=${encodeURIComponent(file.name)}`, true);
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          const pct = Math.round((e.loaded / e.total) * 100);
          pBar.style.width = pct + '%';
          pPct.innerText = pct + '%';
        }
      };
      xhr.onload = () => resolve();
      xhr.onerror = () => resolve();
      xhr.send(file);
    });
  }

  pCard.style.display = 'none';
  loadTrackList();
}

function deleteTrack(encodedName) {
  if (!confirm('Удалить этот трек с консоли?')) return;
  fetch(`/api/delete?name=${encodedName}`, { method: 'POST' })
    .then(() => loadTrackList());
}

loadTrackList();
</script>
</body>
</html>
)rawhtml";

    } // namespace

    MusicServer &MusicServer::Instance() {
        static MusicServer s_instance;
        return s_instance;
    }

    void MusicServer::ResetSession() {
        m_upload_count.store(0, std::memory_order_relaxed);
        m_last_file.clear();
    }

    std::string MusicServer::LastUploadedFile() const {
        return m_last_file;
    }

    bool MusicServer::Start(uint16_t port) {
        if (m_running.load(std::memory_order_acquire)) {
            if (m_port == port) return true;
            Stop();
        }

        mkdir("sdmc:/slaunch", 0777);
        mkdir(kMusicDir, 0777);

        m_port = port;
        m_stop_requested.store(false, std::memory_order_release);

        m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_server_fd < 0) return false;

        int opt = 1;
        setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(m_port);

        if (bind(m_server_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            close(m_server_fd);
            m_server_fd = -1;
            return false;
        }

        if (listen(m_server_fd, 8) < 0) {
            close(m_server_fd);
            m_server_fd = -1;
            return false;
        }

        m_running.store(true, std::memory_order_release);

        Result rc = threadCreate(&m_thread, &MusicServer::ServerThreadTrampoline, this,
                                 nullptr, 0x40000, 0x3B, -2);
        if (R_SUCCEEDED(rc)) {
            m_thread_valid = true;
            threadStart(&m_thread);
            return true;
        }

        close(m_server_fd);
        m_server_fd = -1;
        m_running.store(false, std::memory_order_release);
        return false;
    }

    void MusicServer::Stop() {
        if (!m_running.load(std::memory_order_acquire) && !m_thread_valid) return;
        m_stop_requested.store(true, std::memory_order_release);

        if (m_server_fd >= 0) {
            shutdown(m_server_fd, SHUT_RDWR);
            close(m_server_fd);
            m_server_fd = -1;
        }

        if (m_thread_valid) {
            threadWaitForExit(&m_thread);
            threadClose(&m_thread);
            m_thread_valid = false;
        }
        m_running.store(false, std::memory_order_release);
    }

    void MusicServer::ServerThreadTrampoline(void *arg) {
        static_cast<MusicServer *>(arg)->Run();
    }

    void MusicServer::Run() {
        while (!m_stop_requested.load(std::memory_order_acquire)) {
            struct pollfd pfd{};
            pfd.fd = m_server_fd;
            pfd.events = POLLIN;

            int prc = poll(&pfd, 1, 300);
            if (prc < 0) break;
            if (prc == 0) continue;

            if (pfd.revents & POLLIN) {
                struct sockaddr_in client_addr{};
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(m_server_fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd >= 0) {
                    struct timeval tv{};
                    tv.tv_sec = 6;
                    tv.tv_usec = 0;
                    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

                    HandleClient(client_fd);
                    close(client_fd);
                }
            }
        }
        m_running.store(false, std::memory_order_release);
    }

    void MusicServer::HandleClient(int fd) {
        // Read HTTP request header
        std::string header_data;
        char buf[4096];
        size_t header_end = std::string::npos;

        while (header_end == std::string::npos && header_data.size() < 16384) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            header_data.append(buf, n);
            header_end = header_data.find("\r\n\r\n");
        }

        if (header_end == std::string::npos) return;

        // Parse first line: METHOD PATH HTTP/1.1
        size_t line1_end = header_data.find("\r\n");
        if (line1_end == std::string::npos) return;
        std::string line1 = header_data.substr(0, line1_end);

        char method[16] = {}, path[512] = {};
        if (sscanf(line1.c_str(), "%15s %511s", method, path) != 2) return;

        // Parse Content-Length if present
        size_t content_length = 0;
        {
            const char *needle = "Content-Length:";
            size_t pos = header_data.find(needle);
            if (pos == std::string::npos) {
                needle = "content-length:";
                pos = header_data.find(needle);
            }
            if (pos != std::string::npos) {
                pos += strlen(needle);
                content_length = strtoull(header_data.c_str() + pos, nullptr, 10);
            }
        }

        std::string path_str = path;

        // Route: GET /
        if (strcmp(method, "GET") == 0 && (path_str == "/" || path_str == "/index.html")) {
            char hdr[256];
            snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     sizeof(kIndexHtml) - 1);
            send(fd, hdr, strlen(hdr), 0);
            send(fd, kIndexHtml, sizeof(kIndexHtml) - 1, 0);
            return;
        }

        // Route: GET /api/list
        if (strcmp(method, "GET") == 0 && path_str.rfind("/api/list", 0) == 0) {
            std::string json = "[";
            DIR *d = opendir(kMusicDir);
            if (d) {
                bool first = true;
                struct dirent *e;
                while ((e = readdir(d)) != nullptr) {
                    if (e->d_name[0] == '.') continue;
                    if (!HasAudioExtension(e->d_name)) continue;
                    std::string fpath = std::string(kMusicDir) + "/" + e->d_name;
                    struct stat st{};
                    size_t sz = 0;
                    if (stat(fpath.c_str(), &st) == 0) sz = st.st_size;

                    if (!first) json += ",";
                    first = false;

                    // Escape quotes in name
                    std::string esc_name;
                    for (char c : std::string(e->d_name)) {
                        if (c == '"') esc_name += "\\\"";
                        else esc_name.push_back(c);
                    }
                    char item[512];
                    snprintf(item, sizeof(item), "{\"name\":\"%s\",\"size\":%zu}", esc_name.c_str(), sz);
                    json += item;
                }
                closedir(d);
            }
            json += "]";

            char hdr[256];
            snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     json.size());
            send(fd, hdr, strlen(hdr), 0);
            send(fd, json.data(), json.size(), 0);
            return;
        }

        // Route: POST /api/upload?name=...
        if (strcmp(method, "POST") == 0 && path_str.rfind("/api/upload", 0) == 0) {
            std::string raw_name;
            size_t q = path_str.find("name=");
            if (q != std::string::npos) {
                raw_name = path_str.substr(q + 5);
                size_t amp = raw_name.find('&');
                if (amp != std::string::npos) raw_name = raw_name.substr(0, amp);
            }
            std::string filename = SanitizeFilename(raw_name);
            if (!HasAudioExtension(filename)) filename += ".mp3";

            std::string out_path = std::string(kMusicDir) + "/" + filename;
            FILE *fp = fopen(out_path.c_str(), "wb");
            if (!fp) {
                const char err[] = "HTTP/1.1 500 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(fd, err, sizeof(err) - 1, 0);
                return;
            }

            // Write initial payload after \r\n\r\n
            size_t body_start = header_end + 4;
            size_t initial_bytes = header_data.size() - body_start;
            size_t total_written = 0;

            if (initial_bytes > 0) {
                size_t to_write = std::min(initial_bytes, content_length ? content_length : initial_bytes);
                fwrite(header_data.data() + body_start, 1, to_write, fp);
                total_written += to_write;
            }

            // Stream remainder of body
            while (!m_stop_requested.load(std::memory_order_relaxed)) {
                if (content_length > 0 && total_written >= content_length) break;
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                fwrite(buf, 1, (size_t)n, fp);
                total_written += n;
            }
            fclose(fp);

            m_upload_count.fetch_add(1, std::memory_order_relaxed);
            m_last_file = filename;

            const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
            send(fd, resp, sizeof(resp) - 1, 0);
            return;
        }

        // Route: POST /api/delete?name=...
        if (strcmp(method, "POST") == 0 && path_str.rfind("/api/delete", 0) == 0) {
            std::string raw_name;
            size_t q = path_str.find("name=");
            if (q != std::string::npos) {
                raw_name = path_str.substr(q + 5);
                size_t amp = raw_name.find('&');
                if (amp != std::string::npos) raw_name = raw_name.substr(0, amp);
            }
            std::string filename = SanitizeFilename(raw_name);
            std::string target = std::string(kMusicDir) + "/" + filename;
            unlink(target.c_str());

            const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
            send(fd, resp, sizeof(resp) - 1, 0);
            return;
        }

        // 404 fallback
        const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(fd, nf, sizeof(nf) - 1, 0);
    }

} // namespace sl::menu::net
