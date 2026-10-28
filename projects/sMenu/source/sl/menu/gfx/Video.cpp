#include <sl/menu/gfx/Video.hpp>
#include <sl/menu/gfx/Gfx.hpp>

#ifdef SL_HAS_VIDEO
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}
#include <SDL2/SDL_mixer.h>
#include <webp/demux.h>
#include <cstring>
#include <cmath>
#include <deque>
#include <algorithm>
#endif

namespace sl::menu::gfx {

#ifdef SL_HAS_VIDEO

    namespace {
        // NV12 byte sizes for a given coded size - Y is one byte per pixel,
        // UV is one interleaved U,V byte pair per 2x2 block (half width, half
        // height, two bytes per sample).
        size_t YSize(int w, int h)  { return (size_t)w * (size_t)h; }
        size_t UVSize(int w, int h) { return (size_t)(w / 2) * (size_t)(h / 2) * 2; }

        void CopyPlane(uint8_t *dst, int dst_stride,
                       const uint8_t *src, int src_stride,
                       int width_bytes, int height) {
            for (int y = 0; y < height; y++)
                memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride, width_bytes);
        }

        // Interleaves two half-resolution planar chroma planes (YUV420P's U
        // and V) into one NV12-style UV plane. There is no swscale in this
        // trimmed FFmpeg build (video wallpapers do not need general-purpose
        // colorspace conversion) - this is the one specific reshuffle needed
        // to feed a software-decoded (YUV420P) frame to the same NV12
        // SDL_Texture the hardware path uses, written by hand instead.
        void InterleaveUV(uint8_t *dst, int dst_stride,
                          const uint8_t *u, int u_stride,
                          const uint8_t *v, int v_stride,
                          int cw, int ch) {
            for (int y = 0; y < ch; y++) {
                uint8_t *row = dst + (size_t)y * dst_stride;
                const uint8_t *ur = u + (size_t)y * u_stride;
                const uint8_t *vr = v + (size_t)y * v_stride;
                for (int x = 0; x < cw; x++) {
                    row[x * 2]     = ur[x];
                    row[x * 2 + 1] = vr[x];
                }
            }
        }
        static void DownscaleRGBA(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
            if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !src || !dst) return;
            const int x_ratio = (sw << 16) / dw;
            const int y_ratio = (sh << 16) / dh;
            for (int y = 0; y < dh; y++) {
                const uint32_t *src_row = (const uint32_t *)(src + ((y * y_ratio) >> 16) * sw * 4);
                uint32_t *dst_row = (uint32_t *)(dst + y * dw * 4);
                for (int x = 0; x < dw; x++) {
                    dst_row[x] = src_row[(x * x_ratio) >> 16];
                }
            }
        }
        static void DownscaleNV12(const uint8_t *src_y, int sy_pitch, const uint8_t *src_uv, int suv_pitch,
                                  int sw, int sh,
                                  uint8_t *dst_y, int dy_pitch, uint8_t *dst_uv, int duv_pitch,
                                  int dw, int dh) {
            if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !src_y || !dst_y) return;
            const int xr = (sw << 16) / dw;
            const int yr = (sh << 16) / dh;
            for (int y = 0; y < dh; y++) {
                const uint8_t *sy_row = src_y + ((y * yr) >> 16) * sy_pitch;
                uint8_t *dy_row = dst_y + y * dy_pitch;
                for (int x = 0; x < dw; x++) {
                    dy_row[x] = sy_row[(x * xr) >> 16];
                }
            }
            if (!src_uv || !dst_uv) return;
            const int dh_half = dh / 2;
            const int dw_half = dw / 2;
            for (int y = 0; y < dh_half; y++) {
                const uint8_t *suv_row = src_uv + ((y * yr) >> 16) * suv_pitch;
                uint8_t *duv_row = dst_uv + y * duv_pitch;
                for (int x = 0; x < dw_half; x++) {
                    const int sx = (x * xr) >> 16;
                    duv_row[x * 2]     = suv_row[sx * 2];
                    duv_row[x * 2 + 1] = suv_row[sx * 2 + 1];
                }
            }
        }
        static void DownscaleYUV420PToNV12(const uint8_t *src_y, int sy_pitch,
                                           const uint8_t *src_u, int su_pitch,
                                           const uint8_t *src_v, int sv_pitch,
                                           int sw, int sh,
                                           uint8_t *dst_y, int dy_pitch,
                                           uint8_t *dst_uv, int duv_pitch,
                                           int dw, int dh) {
            if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !src_y || !dst_y) return;
            const int xr = (sw << 16) / dw;
            const int yr = (sh << 16) / dh;
            for (int y = 0; y < dh; y++) {
                const uint8_t *sy_row = src_y + ((y * yr) >> 16) * sy_pitch;
                uint8_t *dy_row = dst_y + y * dy_pitch;
                for (int x = 0; x < dw; x++) {
                    dy_row[x] = sy_row[(x * xr) >> 16];
                }
            }
            if (!src_u || !src_v || !dst_uv) return;
            const int dh_half = dh / 2;
            const int dw_half = dw / 2;
            for (int y = 0; y < dh_half; y++) {
                const int sy_h = (y * yr) >> 16;
                const uint8_t *u_row = src_u + sy_h * su_pitch;
                const uint8_t *v_row = src_v + sy_h * sv_pitch;
                uint8_t *duv_row = dst_uv + y * duv_pitch;
                for (int x = 0; x < dw_half; x++) {
                    const int sx_h = (x * xr) >> 16;
                    duv_row[x * 2]     = u_row[sx_h];
                    duv_row[x * 2 + 1] = v_row[sx_h];
                }
            }
        }
        static int VideoInterruptCb(void *opaque) {
            auto *self = static_cast<VideoPlayer *>(opaque);
            return (self && self->IsStopping()) ? 1 : 0;
        }

        struct GlobalAnimEntry {
            std::string path;
            std::shared_ptr<const std::vector<VideoPlayer::CachedFrame>> frames;
            int width = 0;
            int height = 0;
            u64 last_used = 0;
        };

        class GlobalAnimCache {
        public:
            static constexpr size_t kMaxEntries = 3; // Cache up to 3 recent animated icons in RAM (~20-35 MB total)

            std::shared_ptr<GlobalAnimEntry> Get(const std::string &path) {
                std::lock_guard<std::mutex> lk(m_mx);
                for (auto &e : m_entries) {
                    if (e.path == path) {
                        e.last_used = armGetSystemTick();
                        return std::make_shared<GlobalAnimEntry>(e);
                    }
                }
                return nullptr;
            }

            void Put(const std::string &path,
                     std::shared_ptr<const std::vector<VideoPlayer::CachedFrame>> frames,
                     int width, int height) {
                if (!frames || frames->empty() || path.empty()) return;
                std::lock_guard<std::mutex> lk(m_mx);
                for (auto &e : m_entries) {
                    if (e.path == path) {
                        e.frames = frames;
                        e.width = width;
                        e.height = height;
                        e.last_used = armGetSystemTick();
                        return;
                    }
                }
                if (m_entries.size() >= kMaxEntries) {
                    size_t oldest_idx = 0;
                    u64 oldest_tick = m_entries[0].last_used;
                    for (size_t i = 1; i < m_entries.size(); ++i) {
                        if (m_entries[i].last_used < oldest_tick) {
                            oldest_tick = m_entries[i].last_used;
                            oldest_idx = i;
                        }
                    }
                    m_entries.erase(m_entries.begin() + oldest_idx);
                }
                GlobalAnimEntry entry;
                entry.path = path;
                entry.frames = frames;
                entry.width = width;
                entry.height = height;
                entry.last_used = armGetSystemTick();
                m_entries.push_back(std::move(entry));
            }

            void Clear() {
                std::lock_guard<std::mutex> lk(m_mx);
                m_entries.clear();
            }

        private:
            std::mutex m_mx;
            std::vector<GlobalAnimEntry> m_entries;
        };

        static GlobalAnimCache s_global_anim_cache;
    } // namespace

    bool VideoPlayer::Open(Gfx *gfx, const char *path, bool loop, bool audio, bool hwaccel, int max_dim) {
        Close();
        if (!gfx || !path || !path[0]) return false;
        m_gfx     = gfx;
        m_path    = path;
        m_loop    = loop;
        m_max_dim = max_dim;

        // Check global LRU memory cache for short icon animations
        if (m_max_dim > 0) {
            auto hit = s_global_anim_cache.Get(m_path);
            if (hit && hit->frames && !hit->frames->empty()) {
                m_shared_frame_cache = hit->frames;
                m_width = hit->width;
                m_height = hit->height;
                m_cache_complete = true;
            }
        }

        const size_t path_len = strlen(path);
        m_is_webp = (path_len >= 5 && strcasecmp(path + path_len - 5, ".webp") == 0);
        if (m_is_webp) {
            m_stop.store(false, std::memory_order_release);
            m_paused.store(false);
            m_ended.store(false);
            m_seek_req.store(false);
            m_skip_until = 0.0;
            ClockSet(0.0);
            if (m_cache_complete && m_width > 0 && m_height > 0) {
                const size_t rgba_size = (size_t)(m_width * m_height * 4);
                m_buf[0].rgba.resize(rgba_size);
                m_buf[1].rgba.resize(rgba_size);
            }
            const int cpu_id = (m_max_dim > 0) ? 2 : -2;
            if (R_FAILED(threadCreate(&m_thread, &VideoPlayer::DecodeTrampoline, this,
                                      nullptr, 0x40000, 0x3B, cpu_id))) {
                Close();
                return false;
            }
            threadStart(&m_thread);
            m_thread_running = true;
            return true;
        }

        // FAST PATH for cached MP4 icon: bypass FFmpeg demuxer, I/O, and NVDEC setup entirely!
        if (m_cache_complete && m_width > 0 && m_height > 0) {
            m_shown_buf = -1;
            m_ready_buf.store(-1, std::memory_order_relaxed);
            for (auto &b : m_buf) {
                b.y.assign(YSize(m_width, m_height), 0);
                b.uv.assign(UVSize(m_width, m_height), 128);
                b.y_pitch  = m_width;
                b.uv_pitch = m_width;
            }
            m_tex = SDL_CreateTexture(m_gfx->Renderer(), SDL_PIXELFORMAT_NV12,
                                      SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
            if (!m_tex) { Close(); return false; }
            SDL_SetTextureBlendMode(m_tex, SDL_BLENDMODE_BLEND);

            m_stop.store(false, std::memory_order_release);
            m_paused.store(false);
            m_ended.store(false);
            m_seek_req.store(false);
            m_skip_until = 0.0;
            ClockSet(0.0);
            const int cpu_id = (m_max_dim > 0) ? 2 : -2;
            if (R_FAILED(threadCreate(&m_thread, &VideoPlayer::DecodeTrampoline, this,
                                      nullptr, 0x40000, 0x3B, cpu_id))) {
                Close();
                return false;
            }
            threadStart(&m_thread);
            m_thread_running = true;
            return true;
        }

        // FFmpeg's avformat layer parses its input string as a URL and takes
        // everything before the first ':' as a protocol scheme - so a plain
        // "sdmc:/..." path (every path in this codebase) reads as protocol
        // "sdmc", which is not registered, and open fails with "Protocol not
        // found". The "file:" prefix forces the "file" protocol, which takes
        // the rest of the string as a literal path - colon included - the
        // same way fopen()/IMG_LoadTexture() already treat it everywhere
        // else in this codebase.
        const std::string url = "file:" + m_path;

        m_stop.store(false, std::memory_order_release);
        m_paused.store(false);

        m_fmt = avformat_alloc_context();
        if (!m_fmt) { Close(); return false; }
        m_fmt->interrupt_callback.callback = &VideoInterruptCb;
        m_fmt->interrupt_callback.opaque   = this;

        if (avformat_open_input(&m_fmt, url.c_str(), nullptr, nullptr) < 0) { Close(); return false; }
        if (avformat_find_stream_info(m_fmt, nullptr) < 0) { Close(); return false; }

        m_stream_idx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (m_stream_idx < 0) { Close(); return false; }

        AVStream *stream = m_fmt->streams[m_stream_idx];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) { Close(); return false; }

        m_dec = avcodec_alloc_context3(codec);
        if (!m_dec) { Close(); return false; }
        if (avcodec_parameters_to_context(m_dec, stream->codecpar) < 0) { Close(); return false; }

#ifdef SL_HAS_NVTEGRA
        // Only present when built against averne/FFmpeg's nvtegra branch
        // (the real Switch build - see scripts/build-ffmpeg.sh). The native
        // desktop simulator links the host's stock FFmpeg, which has no such
        // device type, so this block does not exist there at all - not a
        // runtime fallback, a compile-time one. A failure here (e.g. no
        // Tegra hardware, which is only reachable if this somehow ran on
        // non-Switch nvtegra-enabled FFmpeg) is not fatal: m_hw_device_ctx
        // stays null and the decoder falls back to its normal software path.
        if (hwaccel) {
            av_hwdevice_ctx_create(&m_hw_device_ctx, AV_HWDEVICE_TYPE_NVTEGRA, nullptr, nullptr, 0);
            if (m_hw_device_ctx) m_dec->hw_device_ctx = av_buffer_ref(m_hw_device_ctx);
        }
#endif

        if (avcodec_open2(m_dec, codec, nullptr) < 0) { Close(); return false; }

        m_width  = m_dec->width;
        m_height = m_dec->height;
        if (m_max_dim > 0 && (m_width > m_max_dim || m_height > m_max_dim)) {
            if (m_width >= m_height) {
                m_height = std::max(2, (int)(m_height * m_max_dim / m_width) & ~1);
                m_width  = m_max_dim & ~1;
            } else {
                m_width  = std::max(2, (int)(m_width * m_max_dim / m_height) & ~1);
                m_height = m_max_dim & ~1;
            }
        }
        if (m_width <= 0 || m_height <= 0) { Close(); return false; }

        m_duration = (m_fmt->duration > 0) ? (double)m_fmt->duration / AV_TIME_BASE : 0.0;

        // Sound, when asked for: only if the mixer is open as stereo s16 (what
        // Music::Init opens) and the file has a stream FFmpeg can decode. A
        // build without the AAC decoder just plays the picture.
        int mix_rate = 0, mix_ch = 0; Uint16 mix_fmt = 0;
        if (audio && Mix_QuerySpec(&mix_rate, &mix_fmt, &mix_ch) && mix_ch == 2 &&
            mix_fmt == AUDIO_S16SYS) {
            m_astream_idx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            const AVCodec *ac = (m_astream_idx >= 0)
                ? avcodec_find_decoder(m_fmt->streams[m_astream_idx]->codecpar->codec_id) : nullptr;
            if (ac && (m_adec = avcodec_alloc_context3(ac)) &&
                avcodec_parameters_to_context(m_adec, m_fmt->streams[m_astream_idx]->codecpar) >= 0 &&
                avcodec_open2(m_adec, ac, nullptr) >= 0) {
                m_mix_rate = mix_rate;
                m_ring.assign((size_t)mix_rate * 2 * 2, 0);   // two seconds, stereo
                m_ring_r = m_ring_n = 0;
                Mix_SetPostMix(&VideoPlayer::MixTrampoline, this);
                m_mix_hooked = true;
            } else {
                if (m_adec) avcodec_free_context(&m_adec);
                m_astream_idx = -1;
            }
        }

        m_shown_buf = -1;
        m_ready_buf.store(-1, std::memory_order_relaxed);
        for (auto &b : m_buf) {
            b.y.assign(YSize(m_width, m_height), 0);
            b.uv.assign(UVSize(m_width, m_height), 128); // NV12: 128 chroma = black (avoids green frame)
            b.y_pitch  = m_width;
            b.uv_pitch = m_width; // NV12: chroma row is (w/2) U,V pairs = w bytes
        }

        m_tex = SDL_CreateTexture(m_gfx->Renderer(), SDL_PIXELFORMAT_NV12,
                                  SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
        if (!m_tex) { Close(); return false; }
        SDL_SetTextureBlendMode(m_tex, SDL_BLENDMODE_BLEND);

        m_stop.store(false, std::memory_order_release);
        m_paused.store(false);
        m_ended.store(false);
        m_seek_req.store(false);
        m_skip_until = 0.0;
        ClockSet(0.0);
        const int cpu_id = (m_max_dim > 0) ? 2 : -2;
        if (R_FAILED(threadCreate(&m_thread, &VideoPlayer::DecodeTrampoline, this,
                                  nullptr, 0x40000, 0x3B, cpu_id))) {
            Close();
            return false;
        }
        threadStart(&m_thread);
        m_thread_running = true;
        return true;
    }

    void VideoPlayer::Tick() {
        int ready = m_ready_buf.load(std::memory_order_acquire);
        if (ready < 0 || ready == m_shown_buf) return;
        if (m_is_webp) {
            if (!m_tex && m_width > 0 && m_height > 0) {
                m_tex = SDL_CreateTexture(m_gfx->Renderer(), SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
                if (m_tex) SDL_SetTextureBlendMode(m_tex, SDL_BLENDMODE_BLEND);
            }
            if (m_tex) {
                SDL_UpdateTexture(m_tex, nullptr, m_buf[ready].rgba.data(), m_width * 4);
                m_shown_buf = ready;
            }
            return;
        }
        if (!m_tex) return;
        const Buf &b = m_buf[ready];
        SDL_UpdateNVTexture(m_tex, nullptr, b.y.data(), b.y_pitch, b.uv.data(), b.uv_pitch);
        m_shown_buf = ready;
    }

    void VideoPlayer::Close() {
        m_stop.store(true, std::memory_order_release);
        if (m_thread_running) {
            threadWaitForExit(&m_thread);
            threadClose(&m_thread);
            m_thread_running = false;
        }
        if (m_mix_hooked) {
            // Mix_SetPostMix takes the audio lock, so once it returns the
            // callback is not running and will not run again.
            Mix_SetPostMix(nullptr, nullptr);
            m_mix_hooked = false;
        }
        if (m_adec) { avcodec_free_context(&m_adec); m_adec = nullptr; }
        m_astream_idx = -1;
        m_ring.clear(); m_ring_r = m_ring_n = 0; m_audio_debt = 0;
        m_rs_pos = 0.0; m_rs_prev[0] = m_rs_prev[1] = 0.0f;
        m_duration = 0.0;
        if (m_dec) { avcodec_free_context(&m_dec); m_dec = nullptr; }
        if (m_fmt) { avformat_close_input(&m_fmt); m_fmt = nullptr; }
        if (m_hw_device_ctx) { av_buffer_unref(&m_hw_device_ctx); m_hw_device_ctx = nullptr; }
        if (m_tex) { SDL_DestroyTexture(m_tex); m_tex = nullptr; }
        m_stream_idx = -1;
        m_width = m_height = 0;
        m_ready_buf.store(-1, std::memory_order_release);
        m_shown_buf = -1;
        m_buf[0].rgba.clear();
        m_buf[1].rgba.clear();
        m_frame_cache.clear();
        m_frame_cache.shrink_to_fit();
        m_shared_frame_cache.reset();
        m_cache_complete = false;
        m_is_webp = false;
        m_path.clear();
    }

    void VideoPlayer::DecodeTrampoline(void *self) {
        auto *p = static_cast<VideoPlayer *>(self);
        if (p->m_is_webp) {
            p->DecodeLoopWebP();
        } else {
            p->DecodeLoop();
        }
    }

    void VideoPlayer::DecodeLoopWebP() {
        // FAST PATH: If this animation is in global memory cache, replay immediately with 0% CPU!
        if (m_cache_complete && m_shared_frame_cache && !m_shared_frame_cache->empty()) {
            const auto &frames = *m_shared_frame_cache;
            const size_t rgba_size = (size_t)(m_width * m_height * 4);
            int write_buf = 0;
            while (!m_stop.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < frames.size() && !m_stop.load(std::memory_order_relaxed); ++i) {
                    while (m_paused.load(std::memory_order_relaxed) && !m_stop.load(std::memory_order_relaxed)) {
                        svcSleepThread(10'000'000);
                    }
                    if (m_stop.load(std::memory_order_relaxed)) break;
                    const auto &cf = frames[i];
                    if (!m_stop.load(std::memory_order_relaxed)) {
                        memcpy(m_buf[write_buf].rgba.data(), cf.data.data(), rgba_size);
                        m_ready_buf.store(write_buf, std::memory_order_release);
                        write_buf ^= 1;
                    }
                    const int d = (cf.delay_ms >= 10 && cf.delay_ms <= 1000) ? cf.delay_ms : 33;
                    svcSleepThread((u64)d * 1'000'000ULL);
                }
            }
            return;
        }

        FILE *fp = fopen(m_path.c_str(), "rb");
        if (!fp) return;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz <= 0 || sz > 64 * 1024 * 1024 || m_stop.load(std::memory_order_relaxed)) { fclose(fp); return; }
        std::vector<uint8_t> data((size_t)sz);
        if (fread(data.data(), 1, (size_t)sz, fp) != (size_t)sz || m_stop.load(std::memory_order_relaxed)) { fclose(fp); return; }
        fclose(fp);
        if (m_stop.load(std::memory_order_relaxed)) return;

        WebPData webp_data = { data.data(), data.size() };
        WebPAnimDecoderOptions dec_options;
        WebPAnimDecoderOptionsInit(&dec_options);
        dec_options.color_mode = MODE_RGBA;
        dec_options.use_threads = 0; // Avoid thread pool creation overhead on Cortex-A57 for small icons

        WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_data, &dec_options);
        if (!dec) return;

        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(dec, &info) || info.canvas_width == 0 || info.canvas_height == 0 ||
            info.canvas_width > 1920 || info.canvas_height > 1080) {
            WebPAnimDecoderDelete(dec);
            return;
        }

        m_width = (int)info.canvas_width;
        m_height = (int)info.canvas_height;
        m_duration = (double)info.frame_count * 0.05;

        // Downscale target dimensions if max_dim was specified (e.g. 256 for icons)
        int dst_w = m_width;
        int dst_h = m_height;
        if (m_max_dim > 0 && (m_width > m_max_dim || m_height > m_max_dim)) {
            if (m_width >= m_height) {
                dst_w = m_max_dim;
                dst_h = std::max(1, (int)(info.canvas_height * m_max_dim / info.canvas_width));
            } else {
                dst_h = m_max_dim;
                dst_w = std::max(1, (int)(info.canvas_width * m_max_dim / info.canvas_height));
            }
        }
        m_width = dst_w;
        m_height = dst_h;

        const size_t rgba_size = (size_t)(m_width * m_height * 4);
        m_buf[0].rgba.resize(rgba_size);
        m_buf[1].rgba.resize(rgba_size);

        int write_buf = 0;
        int prev_timestamp_ms = 0;
        int accum_delay_ms = 0;
        int skipped_count = 0;
        m_frame_cache.clear();
        m_cache_complete = false;

        while (!m_stop.load(std::memory_order_relaxed)) {
            if (m_paused.load(std::memory_order_relaxed)) {
                svcSleepThread(10'000'000);
                continue;
            }

            // FAST PATH: If all frames were cached during Loop 1, replay directly from RAM with near 0% CPU!
            if (m_cache_complete && m_shared_frame_cache && !m_shared_frame_cache->empty()) {
                const auto &frames = *m_shared_frame_cache;
                for (size_t i = 0; i < frames.size() && !m_stop.load(std::memory_order_relaxed); ++i) {
                    while (m_paused.load(std::memory_order_relaxed) && !m_stop.load(std::memory_order_relaxed)) {
                        svcSleepThread(10'000'000);
                    }
                    if (m_stop.load(std::memory_order_relaxed)) break;
                    const auto &cf = frames[i];
                    if (!m_stop.load(std::memory_order_relaxed)) {
                        memcpy(m_buf[write_buf].rgba.data(), cf.data.data(), rgba_size);
                        m_ready_buf.store(write_buf, std::memory_order_release);
                        write_buf ^= 1;
                    }
                    const int d = (cf.delay_ms >= 10 && cf.delay_ms <= 1000) ? cf.delay_ms : 33;
                    svcSleepThread((u64)d * 1'000'000ULL);
                }
                continue;
            }

            const u64 t_decode_start = armGetSystemTick();
            uint8_t *buf = nullptr;
            int timestamp_ms = 0;
            const bool get_ok = dec && WebPAnimDecoderGetNext(dec, &buf, &timestamp_ms);

            // Cap in-memory frame cache to 60 frames (~2 sec @ 30 FPS) for icons to prevent runaway RAM & CPU on large WebPs
            const bool hit_frame_limit = (m_max_dim > 0 && m_frame_cache.size() >= 60);

            if (!get_ok || hit_frame_limit) {
                if (m_loop) {
                    if (m_max_dim > 0 && !m_frame_cache.empty()) {
                        // Loop 1 finished (or capped): finalize cache into global LRU and free raw file buffer & decoder!
                        m_shared_frame_cache = std::make_shared<const std::vector<CachedFrame>>(std::move(m_frame_cache));
                        m_frame_cache.clear();
                        m_frame_cache.shrink_to_fit();
                        s_global_anim_cache.Put(m_path, m_shared_frame_cache, m_width, m_height);
                        m_cache_complete = true;
                        if (dec) { WebPAnimDecoderDelete(dec); dec = nullptr; }
                        data.clear();
                        data.shrink_to_fit();
                        continue;
                    }
                    if (dec) WebPAnimDecoderReset(dec);
                    prev_timestamp_ms = 0;
                    accum_delay_ms = 0;
                    skipped_count = 0;
                    continue;
                } else {
                    m_ended.store(true);
                    svcSleepThread(10'000'000);
                    continue;
                }
            }

            int frame_delay_ms = timestamp_ms - prev_timestamp_ms;
            if (frame_delay_ms <= 0) frame_delay_ms = 33;
            if (frame_delay_ms > 1000) frame_delay_ms = 1000;
            prev_timestamp_ms = timestamp_ms;

            // Frame-rate throttle for small icons: drop every second frame if file is 60 FPS,
            // but preserve original animation playback speed by accumulating frame delay!
            if (m_max_dim > 0 && frame_delay_ms < 30 && skipped_count == 0) {
                accum_delay_ms += frame_delay_ms;
                skipped_count++;
                continue;
            }
            frame_delay_ms += accum_delay_ms;
            accum_delay_ms = 0;
            skipped_count = 0;

            if (buf && !m_stop.load(std::memory_order_relaxed)) {
                if (dst_w == (int)info.canvas_width && dst_h == (int)info.canvas_height) {
                    memcpy(m_buf[write_buf].rgba.data(), buf, rgba_size);
                } else {
                    DownscaleRGBA(buf, (int)info.canvas_width, (int)info.canvas_height,
                                 m_buf[write_buf].rgba.data(), dst_w, dst_h);
                }
                if (m_max_dim > 0) {
                    CachedFrame cf;
                    cf.pts  = (double)timestamp_ms / 1000.0;
                    cf.delay_ms = frame_delay_ms;
                    cf.data = m_buf[write_buf].rgba;
                    m_frame_cache.push_back(std::move(cf));
                }
                m_ready_buf.store(write_buf, std::memory_order_release);
                write_buf ^= 1;

                // Subtract decode execution time from target delay so display duration is exactly 1.0x natural speed!
                const u64 decode_elapsed_ns = (armGetSystemTick() - t_decode_start) * 1000000000ULL / armGetSystemTickFreq();
                const u64 target_ns = (u64)frame_delay_ms * 1000000ULL;
                if (target_ns > decode_elapsed_ns + 1000000ULL) {
                    svcSleepThread(target_ns - decode_elapsed_ns);
                }
            }
        }

        if (dec) WebPAnimDecoderDelete(dec);
    }

    // ---- clock (lock-free) ----------------------------------------------------
    double VideoPlayer::ClockNow() const {
        if (m_paused.load(std::memory_order_relaxed))
            return m_pause_at.load(std::memory_order_relaxed);
        const u64 tick = m_clock_tick.load(std::memory_order_relaxed);
        const double base = m_clock_base.load(std::memory_order_relaxed);
        return base + (double)(armGetSystemTick() - tick) / (double)armGetSystemTickFreq();
    }
    void VideoPlayer::ClockSet(double seconds) {
        m_clock_base.store(seconds, std::memory_order_relaxed);
        m_pause_at.store(seconds, std::memory_order_relaxed);
        m_clock_tick.store(armGetSystemTick(), std::memory_order_relaxed);
    }
    void VideoPlayer::SetPaused(bool paused) {
        if (paused == m_paused.load(std::memory_order_relaxed)) return;
        const u64 now = armGetSystemTick();
        if (paused) {
            const u64 tick = m_clock_tick.load(std::memory_order_relaxed);
            const double base = m_clock_base.load(std::memory_order_relaxed);
            m_pause_at.store(base + (double)(now - tick) / (double)armGetSystemTickFreq(), std::memory_order_relaxed);
        } else {
            m_clock_base.store(m_pause_at.load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_clock_tick.store(now, std::memory_order_relaxed);
        }
        m_paused.store(paused, std::memory_order_release);
    }
    double VideoPlayer::Position() const {
        if (!m_fmt) return 0.0;
        const double p = ClockNow();
        return m_duration > 0.0 ? std::min(std::max(p, 0.0), m_duration) : std::max(p, 0.0);
    }
    void VideoPlayer::Seek(double seconds) {
        if (!m_fmt) return;
        if (m_duration > 0.0) seconds = std::min(seconds, m_duration - 0.1);
        m_seek_to.store(std::max(0.0, seconds));
        m_seek_req.store(true);
    }

    // Worker side of a seek: land on the keyframe at or before the target,
    // then decode forward silently (m_skip_until) so the first picture and
    // sound are exactly at it.
    void VideoPlayer::DoSeek(double seconds) {
        const AVStream *vs = m_fmt->streams[m_stream_idx];
        const int64_t start = (vs->start_time != AV_NOPTS_VALUE) ? vs->start_time : 0;
        const int64_t ts = start + (int64_t)(seconds / av_q2d(vs->time_base));
        av_seek_frame(m_fmt, m_stream_idx, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_dec);
        if (m_adec) avcodec_flush_buffers(m_adec);
        {
            std::lock_guard<std::mutex> lk(m_ring_mx);
            m_ring_r = m_ring_n = 0;
            m_audio_debt = 0;
        }
        m_rs_pos = 0.0;
        m_skip_until = seconds;
        m_ended.store(false);
        ClockSet(seconds);
    }

    // ---- audio ------------------------------------------------------------------
    // Decoded audio in, stereo s16 at the mixer's rate into the ring. There is
    // no swresample in this FFmpeg build, so the two conversions it would do
    // are done here: planar/packed float or s16 to stereo float, then a
    // linear resample (Switch clips are 48 kHz, the mixer runs at 44.1).
    void VideoPlayer::DecodeAudio(AVPacket *pkt, AVFrame *frame) {
        if (avcodec_send_packet(m_adec, pkt) < 0) return;
        const AVStream *as = m_fmt->streams[m_astream_idx];
        while (!m_stop.load() && !m_seek_req.load() &&
               avcodec_receive_frame(m_adec, frame) == 0) {
            const int n  = frame->nb_samples;
            const int ch = frame->ch_layout.nb_channels;
            const int rate = frame->sample_rate > 0 ? frame->sample_rate : m_mix_rate;
            const double pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                ? (frame->best_effort_timestamp - (as->start_time != AV_NOPTS_VALUE ? as->start_time : 0))
                  * av_q2d(as->time_base) : m_skip_until;
            if (n <= 0 || ch <= 0 || pts + (double)n / rate < m_skip_until) { av_frame_unref(frame); continue; }

            auto sample = [&](int c, int i) -> float {
                c = std::min(c, ch - 1);
                switch (frame->format) {
                    case AV_SAMPLE_FMT_FLTP: return ((const float *)frame->data[c])[i];
                    case AV_SAMPLE_FMT_FLT:  return ((const float *)frame->data[0])[i * ch + c];
                    case AV_SAMPLE_FMT_S16P: return ((const int16_t *)frame->data[c])[i] / 32768.0f;
                    case AV_SAMPLE_FMT_S16:  return ((const int16_t *)frame->data[0])[i * ch + c] / 32768.0f;
                    default:                 return 0.0f;
                }
            };

            const double step = (double)rate / (double)m_mix_rate;
            std::vector<int16_t> out;
            out.reserve((size_t)(n / step + 2) * 2);
            // Position runs over [prev, in0 .. in(n-1)]: index 0 is the last
            // frame of the previous packet, so the resample is seamless.
            for (; m_rs_pos + 1.0 < (double)n + 1.0; m_rs_pos += step) {
                const int   i = (int)m_rs_pos;
                const float f = (float)(m_rs_pos - i);
                for (int c = 0; c < 2; c++) {
                    const float a = (i == 0) ? m_rs_prev[c] : sample(c, i - 1);
                    const float b = sample(c, i);
                    const float v = std::min(1.0f, std::max(-1.0f, a + (b - a) * f));
                    out.push_back((int16_t)lrintf(v * 32767.0f));
                }
            }
            m_rs_pos -= (double)n;
            m_rs_prev[0] = sample(0, n - 1);
            m_rs_prev[1] = sample(1, n - 1);
            av_frame_unref(frame);

            size_t frames = out.size() / 2, off = 0;
            const size_t cap = m_ring.size() / 2;
            while (frames > 0 && !m_stop.load() && !m_seek_req.load()) {
                std::unique_lock<std::mutex> lk(m_ring_mx);
                // Pay back an underrun by skipping what the mixer missed.
                const size_t skip = std::min(frames, m_audio_debt);
                m_audio_debt -= skip; off += skip; frames -= skip;
                const size_t room = std::min(frames, cap - m_ring_n);
                for (size_t k = 0; k < room; k++) {
                    const size_t w = ((m_ring_r + m_ring_n + k) % cap) * 2;
                    m_ring[w]     = out[(off + k) * 2];
                    m_ring[w + 1] = out[(off + k) * 2 + 1];
                }
                m_ring_n += room; off += room; frames -= room;
                lk.unlock();
                if (frames > 0) svcSleepThread(5'000'000);   // ring full: let it drain
            }
        }
    }

    void VideoPlayer::MixTrampoline(void *self, Uint8 *stream, int len) {
        static_cast<VideoPlayer *>(self)->Mix(stream, len);
    }
    // Mixer thread: add the clip's sound on top of whatever else is playing.
    void VideoPlayer::Mix(Uint8 *stream, int len) {
        if (m_paused.load() || m_ring.empty()) return;
        int16_t *dst = (int16_t *)stream;
        const size_t want = (size_t)len / 4, cap = m_ring.size() / 2;
        std::lock_guard<std::mutex> lk(m_ring_mx);
        const size_t got = std::min(want, m_ring_n);
        for (size_t k = 0; k < got; k++) {
            const size_t r = ((m_ring_r + k) % cap) * 2;
            for (int c = 0; c < 2; c++) {
                const int v = dst[k * 2 + c] + m_ring[r + c];
                dst[k * 2 + c] = (int16_t)std::min(32767, std::max(-32768, v));
            }
        }
        m_ring_r = (m_ring_r + got) % cap;
        m_ring_n -= got;
        // Only a real underrun counts as debt - running dry after the last
        // packet is simply the end of the clip.
        if (got < want && !m_ended.load()) m_audio_debt += want - got;
    }

    // ---- worker -----------------------------------------------------------------
    // Demuxing runs ahead of presentation. Packets are read until about
    // 0.6 s of sound is buffered (or, with no sound, a few video packets are
    // queued): sound is decoded as soon as it is read, video packets wait in
    // a queue and are decoded only when the picture before them has been
    // shown. Reading one packet per shown frame, as this used to, kept the
    // sound only a frame or two ahead of the mixer - which takes it about a
    // tenth of a second at a time - so it kept running dry: choppy audio.
    void VideoPlayer::DecodeLoop() {
        // FAST PATH: If all frames were cached during Loop 1 (for icons, m_max_dim > 0), replay from RAM with near 0% CPU!
        if (m_cache_complete && m_shared_frame_cache && !m_shared_frame_cache->empty()) {
            const auto &frames = *m_shared_frame_cache;
            int write_buf = 0;
            while (!m_stop.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < frames.size() && !m_stop.load(std::memory_order_relaxed); ++i) {
                    while (m_paused.load(std::memory_order_relaxed) && !m_stop.load(std::memory_order_relaxed)) {
                        svcSleepThread(10'000'000);
                    }
                    if (m_stop.load(std::memory_order_relaxed)) break;
                    const auto &cf = frames[i];
                    if (!m_stop.load(std::memory_order_relaxed)) {
                        Buf &b = m_buf[write_buf];
                        memcpy(b.y.data(), cf.data.data(), cf.data.size());
                        memcpy(b.uv.data(), cf.uv.data(), cf.uv.size());
                        m_ready_buf.store(write_buf, std::memory_order_release);
                        write_buf ^= 1;
                    }
                    const int d = (cf.delay_ms >= 10 && cf.delay_ms <= 1000) ? cf.delay_ms : 33;
                    svcSleepThread((u64)d * 1'000'000ULL);
                }
            }
            return;
        }

        if (!m_fmt || m_stream_idx < 0) return;
        AVStream *stream = m_fmt->streams[m_stream_idx];
        const int64_t vstart = (stream->start_time != AV_NOPTS_VALUE) ? stream->start_time : 0;
        const double  vtb    = av_q2d(stream->time_base);

        AVPacket *pkt       = av_packet_alloc();
        AVFrame  *dec_frame = av_frame_alloc();
        AVFrame  *sw_frame  = av_frame_alloc();
        AVFrame  *a_frame   = av_frame_alloc();
        std::deque<AVPacket *> vq;          // video packets read but not decoded
        bool   read_eof  = false;           // demuxer is done
        bool   dec_eof   = false;           // decoder has handed back its last frame
        bool   drain_sent = false;          // null packet sent to flush the decoder
        bool   have_frame = false;          // dec_frame holds the next picture
        double frame_pts  = 0.0;
        double last_frame_pts = -1.0;
        double prev_frame_pts = -1.0;
        int    write_buf  = 0;

        auto clear_queue = [&]() {
            for (AVPacket *q : vq) av_packet_free(&q);
            vq.clear();
            if (have_frame) { av_frame_unref(dec_frame); have_frame = false; }
            read_eof = dec_eof = drain_sent = false;
            last_frame_pts = -1.0;
            prev_frame_pts = -1.0;
        };
        auto audio_buffered = [&]() -> double {
            if (!m_adec || m_mix_rate <= 0) return 1e9;
            std::lock_guard<std::mutex> lk(m_ring_mx);
            return (double)m_ring_n / m_mix_rate;
        };

        while (pkt && dec_frame && sw_frame && a_frame && !m_stop.load(std::memory_order_acquire)) {
            if (m_seek_req.exchange(false)) { clear_queue(); DoSeek(m_seek_to.load()); }

            // FAST PATH: If all frames were cached during Loop 1 (for icons, m_max_dim > 0), replay from RAM with near 0% CPU!
            if (m_cache_complete && m_shared_frame_cache && !m_shared_frame_cache->empty()) {
                const auto &frames = *m_shared_frame_cache;
                for (size_t i = 0; i < frames.size() && !m_stop.load(std::memory_order_acquire); ++i) {
                    while (m_paused.load(std::memory_order_relaxed) && !m_stop.load(std::memory_order_relaxed)) {
                        svcSleepThread(10'000'000);
                    }
                    if (m_stop.load(std::memory_order_relaxed)) break;
                    const auto &cf = frames[i];
                    if (!m_stop.load(std::memory_order_relaxed)) {
                        Buf &b = m_buf[write_buf];
                        memcpy(b.y.data(), cf.data.data(), cf.data.size());
                        memcpy(b.uv.data(), cf.uv.data(), cf.uv.size());
                        m_ready_buf.store(write_buf, std::memory_order_release);
                        write_buf ^= 1;
                    }
                    const int d = (cf.delay_ms >= 10 && cf.delay_ms <= 1000) ? cf.delay_ms : 33;
                    svcSleepThread((u64)d * 1'000'000ULL);
                }
                continue;
            }

            // ---- read ahead -------------------------------------------------
            while (!read_eof && !m_stop.load() && !m_seek_req.load() &&
                   vq.size() < 16 &&
                   (audio_buffered() < 0.6 || (vq.size() < 4 && !have_frame))) {
                if (av_read_frame(m_fmt, pkt) < 0) { read_eof = true; break; }
                if (pkt->stream_index == m_astream_idx && m_adec) {
                    DecodeAudio(pkt, a_frame);
                    av_packet_unref(pkt);
                } else if (pkt->stream_index == m_stream_idx) {
                    AVPacket *q = av_packet_alloc();
                    if (q) { av_packet_move_ref(q, pkt); vq.push_back(q); }
                    else   av_packet_unref(pkt);
                } else {
                    av_packet_unref(pkt);
                }
            }

            // ---- next picture -----------------------------------------------
            while (!have_frame && !dec_eof && !m_stop.load() && !m_seek_req.load()) {
                const int r = avcodec_receive_frame(m_dec, dec_frame);
                if (r == 0) {
                    frame_pts = (dec_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        ? (dec_frame->best_effort_timestamp - vstart) * vtb : ClockNow();
                    if (frame_pts < m_skip_until - 0.001) { av_frame_unref(dec_frame); continue; }
                    // Frame-rate throttle: cap background / looped videos and small icons to max ~30 FPS
                    const bool throttle_fps = (m_max_dim > 0) || (m_loop && m_astream_idx < 0);
                    if (throttle_fps && last_frame_pts >= 0.0 && (frame_pts - last_frame_pts) < 0.033) {
                        av_frame_unref(dec_frame);
                        const double now = ClockNow();
                        if (frame_pts > now) {
                            const double diff = frame_pts - now;
                            if (diff > 0.002) svcSleepThread((u64)((diff - 0.001) * 1e9));
                        }
                        continue;
                    }
                    prev_frame_pts = last_frame_pts;
                    last_frame_pts = frame_pts;
                    have_frame = true;
                } else if (r == AVERROR_EOF) {
                    dec_eof = true;
                } else if (!vq.empty()) {                 // EAGAIN: feed it
                    AVPacket *q = vq.front(); vq.pop_front();
                    avcodec_send_packet(m_dec, q);
                    av_packet_free(&q);
                } else if (read_eof && !drain_sent) {     // flush out the last frames
                    avcodec_send_packet(m_dec, nullptr);
                    drain_sent = true;
                } else {
                    break;                                 // wait for more packets
                }
            }

            if (have_frame) {
                const double now = ClockNow();
                if (now > frame_pts + 0.08) {
                    ClockSet(frame_pts);
                } else if (now < frame_pts) {
                    const double diff = frame_pts - now;
                    if (diff > 0.002) {
                        svcSleepThread((u64)((diff - 0.001) * 1e9));
                        continue;
                    }
                }

                AVFrame *src = dec_frame;
#ifdef SL_HAS_NVTEGRA
                if (dec_frame->format == AV_PIX_FMT_NVTEGRA) {
                    sw_frame->format = AV_PIX_FMT_NV12;
                    src = (av_hwframe_transfer_data(sw_frame, dec_frame, 0) == 0) ? sw_frame : nullptr;
                }
#endif
                if (src) {
                    Buf &b = m_buf[write_buf];
                    const bool need_scale = (m_width != dec_frame->width || m_height != dec_frame->height);
                    if (src->format == AV_PIX_FMT_NV12) {
                        if (need_scale) {
                            DownscaleNV12(src->data[0], src->linesize[0], src->data[1], src->linesize[1],
                                          dec_frame->width, dec_frame->height,
                                          b.y.data(), b.y_pitch, b.uv.data(), b.uv_pitch,
                                          m_width, m_height);
                        } else {
                            CopyPlane(b.y.data(), b.y_pitch, src->data[0], src->linesize[0], m_width, m_height);
                            CopyPlane(b.uv.data(), b.uv_pitch, src->data[1], src->linesize[1], m_width, m_height / 2);
                        }
                        if (m_max_dim > 0 && m_frame_cache.size() < 60) {
                            CachedFrame cf;
                            cf.pts  = frame_pts;
                            cf.delay_ms = (prev_frame_pts >= 0.0 && frame_pts > prev_frame_pts)
                                ? std::clamp((int)((frame_pts - prev_frame_pts) * 1000.0), 10, 500) : 33;
                            cf.data = b.y;
                            cf.uv   = b.uv;
                            m_frame_cache.push_back(std::move(cf));
                        }
                        m_ready_buf.store(write_buf, std::memory_order_release);
                        write_buf ^= 1;
                    } else if (src->format == AV_PIX_FMT_YUV420P) {
                        if (need_scale) {
                            DownscaleYUV420PToNV12(src->data[0], src->linesize[0],
                                                   src->data[1], src->linesize[1],
                                                   src->data[2], src->linesize[2],
                                                   dec_frame->width, dec_frame->height,
                                                   b.y.data(), b.y_pitch, b.uv.data(), b.uv_pitch,
                                                   m_width, m_height);
                        } else {
                            CopyPlane(b.y.data(), b.y_pitch, src->data[0], src->linesize[0], m_width, m_height);
                            InterleaveUV(b.uv.data(), b.uv_pitch, src->data[1], src->linesize[1],
                                         src->data[2], src->linesize[2], m_width / 2, m_height / 2);
                        }
                        if (m_max_dim > 0 && m_frame_cache.size() < 60) {
                            CachedFrame cf;
                            cf.pts  = frame_pts;
                            cf.delay_ms = (prev_frame_pts >= 0.0 && frame_pts > prev_frame_pts)
                                ? std::clamp((int)((frame_pts - prev_frame_pts) * 1000.0), 10, 500) : 33;
                            cf.data = b.y;
                            cf.uv   = b.uv;
                            m_frame_cache.push_back(std::move(cf));
                        }
                        m_ready_buf.store(write_buf, std::memory_order_release);
                        write_buf ^= 1;
                    }
                    // Any other pixel format (e.g. 10-bit) is dropped - out of
                    // scope (8-bit H.264 only).
                }
                av_frame_unref(dec_frame);
                av_frame_unref(sw_frame);
                have_frame = false;
                continue;
            }

            // Nothing left to show: loop, or hold the last frame until the
            // clock reaches the end.
            if (read_eof && vq.empty() && (dec_eof || drain_sent)) {
                if (dec_eof && m_loop) {
                    if (m_max_dim > 0 && !m_frame_cache.empty()) {
                        m_shared_frame_cache = std::make_shared<const std::vector<CachedFrame>>(std::move(m_frame_cache));
                        m_frame_cache.clear();
                        m_frame_cache.shrink_to_fit();
                        s_global_anim_cache.Put(m_path, m_shared_frame_cache, m_width, m_height);
                        m_cache_complete = true;
                        clear_queue();
                        ClockSet(0.0);
                        continue;
                    }
                    clear_queue();
                    DoSeek(0.0);
                    continue;
                }
                if (ClockNow() >= m_duration - 0.05) m_ended.store(true);
            }
            svcSleepThread(5'000'000);
        }

        for (AVPacket *q : vq) av_packet_free(&q);
        av_frame_free(&a_frame);
        av_frame_free(&sw_frame);
        av_frame_free(&dec_frame);
        av_packet_free(&pkt);
    }

    SDL_Surface *VideoExtractFirstFrame(const char *path, int target_w, int target_h) {
        if (!path || !path[0]) return nullptr;
        const std::string url = "file:" + std::string(path);

        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, url.c_str(), nullptr, nullptr) < 0) {
            return nullptr;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            return nullptr;
        }

        int s_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (s_idx < 0) {
            avformat_close_input(&fmt);
            return nullptr;
        }

        AVStream *stream = fmt->streams[s_idx];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            avformat_close_input(&fmt);
            return nullptr;
        }

        AVCodecContext *dec = avcodec_alloc_context3(codec);
        if (!dec) {
            avformat_close_input(&fmt);
            return nullptr;
        }

        if (avcodec_parameters_to_context(dec, stream->codecpar) < 0 ||
            avcodec_open2(dec, codec, nullptr) < 0) {
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            return nullptr;
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        if (!pkt || !frame) {
            if (pkt) av_packet_free(&pkt);
            if (frame) av_frame_free(&frame);
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            return nullptr;
        }
        SDL_Surface *surf = nullptr;

        int dw = (target_w > 0) ? target_w : dec->width;
        int dh = (target_h > 0) ? target_h : dec->height;
        if (dw <= 0) dw = 256;
        if (dh <= 0) dh = 256;

        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == s_idx) {
                if (avcodec_send_packet(dec, pkt) >= 0) {
                    if (avcodec_receive_frame(dec, frame) >= 0) {
                        const bool is_nv12 = (frame->format == AV_PIX_FMT_NV12);
                        const bool is_yuv420p = (frame->format == AV_PIX_FMT_YUV420P);
                        if (!is_nv12 && !is_yuv420p) {
                            av_frame_unref(frame);
                            av_packet_unref(pkt);
                            break;
                        }
                        const uint8_t *y_plane = frame->data[0];
                        const uint8_t *u_plane = frame->data[1];
                        const uint8_t *v_plane = is_nv12 ? nullptr : frame->data[2];
                        if (!y_plane || !u_plane || (!is_nv12 && !v_plane)) {
                            av_frame_unref(frame);
                            av_packet_unref(pkt);
                            break;
                        }

                        surf = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_RGBA8888);
                        if (surf) {
                            const int sw = frame->width;
                            const int sh = frame->height;
                            const int x_ratio = (sw << 16) / dw;
                            const int y_ratio = (sh << 16) / dh;
                            uint32_t *dst = (uint32_t *)surf->pixels;

                            auto clamp8 = [](int v) -> uint8_t {
                                return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                            };

                            const int y_pitch = frame->linesize[0];
                            const int uv_pitch = frame->linesize[1];

                            for (int y = 0; y < dh; y++) {
                                const int sy = (y * y_ratio) >> 16;
                                const int sy_half = sy >> 1;
                                const uint8_t *y_row = y_plane + sy * y_pitch;
                                uint32_t *d_row = dst + y * dw;

                                for (int x = 0; x < dw; x++) {
                                    const int sx = (x * x_ratio) >> 16;
                                    const int sx_half = sx >> 1;
                                    const int Y = y_row[sx];
                                    int U = 128, V = 128;
                                    if (is_nv12) {
                                        U = u_plane[sy_half * uv_pitch + sx_half * 2];
                                        V = u_plane[sy_half * uv_pitch + sx_half * 2 + 1];
                                    } else if (v_plane) {
                                        U = u_plane[sy_half * uv_pitch + sx_half];
                                        V = v_plane[sy_half * frame->linesize[2] + sx_half];
                                    }
                                    const int C = Y - 16;
                                    const int D = U - 128;
                                    const int E = V - 128;
                                    const uint8_t R = clamp8((298 * C + 409 * E + 128) >> 8);
                                    const uint8_t G = clamp8((298 * C - 100 * D - 208 * E + 128) >> 8);
                                    const uint8_t B = clamp8((298 * C + 516 * D + 128) >> 8);
                                    d_row[x] = ((uint32_t)R << 24) | ((uint32_t)G << 16) | ((uint32_t)B << 8) | 0xFF;
                                }
                            }
                        }
                        av_packet_unref(pkt);
                        break;
                    }
                }
            }
            av_packet_unref(pkt);
        }

        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return surf;
    }

    SDL_Surface *WebPExtractFirstFrame(const char *path, int target_w, int target_h) {
        if (!path || !path[0]) return nullptr;
        FILE *fp = fopen(path, "rb");
        if (!fp) return nullptr;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz <= 0 || sz > 64 * 1024 * 1024) { fclose(fp); return nullptr; }
        std::vector<uint8_t> data((size_t)sz);
        if (fread(data.data(), 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return nullptr; }
        fclose(fp);

        WebPData webp_data = { data.data(), data.size() };
        WebPAnimDecoderOptions dec_options;
        WebPAnimDecoderOptionsInit(&dec_options);
        dec_options.color_mode = MODE_RGBA;
        dec_options.use_threads = 0;

        WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_data, &dec_options);
        if (!dec) return nullptr;

        WebPAnimInfo info;
        if (!WebPAnimDecoderGetInfo(dec, &info) || info.canvas_width == 0 || info.canvas_height == 0) {
            WebPAnimDecoderDelete(dec);
            return nullptr;
        }

        uint8_t *buf = nullptr;
        int timestamp_ms = 0;
        if (!WebPAnimDecoderGetNext(dec, &buf, &timestamp_ms) || !buf) {
            WebPAnimDecoderDelete(dec);
            return nullptr;
        }

        int dw = (target_w > 0) ? target_w : (int)info.canvas_width;
        int dh = (target_h > 0) ? target_h : (int)info.canvas_height;
        if (dw <= 0) dw = 256;
        if (dh <= 0) dh = 256;

        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_RGBA8888);
        if (surf) {
            const int sw = (int)info.canvas_width;
            const int sh = (int)info.canvas_height;
            const int x_ratio = (sw << 16) / dw;
            const int y_ratio = (sh << 16) / dh;
            uint32_t *dst = (uint32_t *)surf->pixels;

            for (int y = 0; y < dh; y++) {
                const int sy = (y * y_ratio) >> 16;
                const uint8_t *src_row = buf + (sy * sw * 4);
                uint32_t *d_row = dst + y * dw;
                for (int x = 0; x < dw; x++) {
                    const int sx = (x * x_ratio) >> 16;
                    const uint8_t *p = src_row + (sx * 4);
                    d_row[x] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
                }
            }
        }

        WebPAnimDecoderDelete(dec);
        return surf;
    }

#else // !SL_HAS_VIDEO - stub for builds with no FFmpeg (e.g. the Windows
      // simulator, which does not stage FFmpeg for MinGW; see
      // projects/sMenu/sim/Makefile).

    bool VideoPlayer::Open(Gfx *, const char *, bool, bool, bool, int) { return false; }
    void VideoPlayer::Tick() {}
    void VideoPlayer::Close() {}
    void VideoPlayer::SetPaused(bool p) { m_paused.store(p); }
    void VideoPlayer::Seek(double) {}
    double VideoPlayer::Position() const { return 0.0; }
    SDL_Surface *VideoExtractFirstFrame(const char *, int, int) { return nullptr; }
    SDL_Surface *WebPExtractFirstFrame(const char *, int, int) { return nullptr; }

#endif

} // namespace sl::menu::gfx
