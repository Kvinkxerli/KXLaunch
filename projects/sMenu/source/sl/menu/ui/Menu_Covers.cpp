#include <sl/menu/ui/Menu.hpp>
#include <unordered_set>
#include <sl/menu/ui/Locale.hpp>
#include <sl/menu/net/Http.hpp>
#include <sl/menu/net/ContentFilter.hpp>
#include <sl/smi/Protocol.hpp>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include "Menu_Internal.hpp"

namespace sl::menu::ui {

    // Box front art for an entry, or nullptr for a blank case.
    //
    // This is deliberately *not* the title's square icon. A 1:1 icon stretched
    // onto a 2:3 front is what made the row look like rotated icons rather than
    // boxes, which is the whole thing we are trying to get away from. Only real
    // cover art goes on a box front; everything else stays a blank case until
    // art arrives.
    //
    // covers/<titleid>.jpg is also where a SteamGridDB fetch would land, so
    // wiring that up later needs no change here.
    SDL_Texture *Menu::FlowCover(const MenuItem &it) {
        if (it.kind != ItemKind::Game || it.app_id == 0) return nullptr;
        auto f = m_covers.find(it.app_id);
        if (f != m_covers.end()) return f->second;   // nullptr is cached too
        QueueArt(Art_Cover, it.app_id);              // lands in a frame or two
        return nullptr;
    }
    SDL_Texture *Menu::GameWrap(const MenuItem &it) {
        if (it.kind != ItemKind::Game || it.app_id == 0) return nullptr;
        auto f = m_game_wraps.find(it.app_id);
        if (f != m_game_wraps.end()) return f->second;   // nullptr is cached too
        QueueArt(Art_Wrap, it.app_id);
        return nullptr;
    }

    // ---- background art loader ----------------------------------------------
    // Everything that touches the card or inflates an image runs here: the
    // stat, the texture-cache read, and on a miss the decode and the write
    // back. Only surfaces come out; textures need the renderer, so PollArt
    // uploads them on the main thread.
    void Menu::QueueArt(int kind, u64 id) {
        if (!m_art_pending.insert({kind, id}).second) return;   // already asked
        if (!StartArt()) { m_art_pending.erase({kind, id}); return; }
        std::lock_guard<std::mutex> lk(m_art_mx);
        m_art_q.push_back(ArtJob{ id, kind, m_art_epoch, nullptr, false });
        // Scrolling through a big library asks for far more than anyone will
        // stop on. The oldest requests are the boxes already scrolled past, so
        // they go; they are asked for again if they come back into view.
        while (m_art_q.size() > 24) {
            m_art_pending.erase({m_art_q.front().kind, m_art_q.front().id});
            m_art_q.pop_front();
        }
        m_art_cv.notify_one();
    }
    bool Menu::StartArt() {
        if (m_art_started) return true;
        // Core 1, away from the render thread on core 0: at a lower priority
        // on the same core it only ever ran in the gaps between frames, which
        // is why art trickled in. Core 0 if 1 is refused.
        if (R_FAILED(threadCreate(&m_art_thread, &Menu::ArtTrampoline, this,
                                  nullptr, 0x20000, 0x3B, 1)) &&
            R_FAILED(threadCreate(&m_art_thread, &Menu::ArtTrampoline, this,
                                  nullptr, 0x20000, 0x3B, -2)))
            return false;
        threadStart(&m_art_thread);
        m_art_started = true;
        return true;
    }
    // Every installed game, in menu order. Called again whenever the app list
    // changes; titles already cached cost a stat and a header read each.
    void Menu::WarmArt() {
        m_art_warm_queued = true;
        std::vector<u64> ids;
        for (const auto &it : m_items)
            if (it.kind == ItemKind::Game && it.app_id != 0) ids.push_back(it.app_id);
        if (ids.empty() || !StartArt()) return;
        std::reverse(ids.begin(), ids.end());      // taken from the back
        std::lock_guard<std::mutex> lk(m_art_mx);
        m_art_warm = std::move(ids);
        m_art_cv.notify_one();
    }
    void Menu::HoldArt(bool hold) {
        std::lock_guard<std::mutex> lk(m_art_mx);
        if (m_art_hold == hold) return;
        m_art_hold = hold;
        m_art_cv.notify_one();
    }
    SDL_Surface *Menu::LoadArt(int kind, u64 id, bool warm, bool &built) {
        static const char *const kSuffix[] = { ".jpg", "_wrap.png", "_hero.jpg" };
        static const char *const kKey[]    = { "", "_wrap", "_hero" };
        static const int kW[] = { kCoverTexW, 700, kDeckHeroW };
        static const int kH[] = { kCoverTexH, 540, kDeckRowH };
        built = false;
        char path[96], key[32];
        snprintf(path, sizeof(path), "sdmc:/slaunch/covers/%016llX%s",
                 (unsigned long long)id, kSuffix[kind]);
        snprintf(key, sizeof(key), "%016llX%s", (unsigned long long)id, kKey[kind]);
        struct stat src {};
        if (stat(path, &src) != 0) return nullptr;   // no file: a null result, remembered
        const std::string cpath = TexCachePath(key);
        const int w = kW[kind], h = kH[kind];
        if (warm && CoverCacheFresh(cpath.c_str(), src, w, h)) return nullptr;

        SDL_Surface *surf = warm ? nullptr : ReadCoverSurf(cpath.c_str(), src, w, h);
        if (!surf) {
            // Hero art is 3.1:1 and its tile 16:9, so it is cropped; covers
            // and box scans already have their tile's shape.
            surf = kind == Art_Hero ? DecodeCoverSurfaceCropped(path, w, h, 0.5f)
                                    : DecodeCoverSurface(path, w, h);
            if (surf) { WriteCoverTex(cpath.c_str(), src, surf); built = true; }
        }
        if (warm) { if (surf) SDL_FreeSurface(surf); return nullptr; }
        // SDL's GLES2 renderer has no RGB565 texture, so uploading one converts
        // it pixel by pixel on the main thread. Converting here leaves the
        // upload a plain copy.
        if (surf) {
            SDL_Surface *conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
            if (conv) { SDL_FreeSurface(surf); surf = conv; }
        }
        return surf;
    }
    void Menu::ArtTrampoline(void *self) {
        Menu *m = static_cast<Menu *>(self);
        for (;;) {
            ArtJob j {};
            u64 warm_id = 0;
            {
                std::unique_lock<std::mutex> lk(m->m_art_mx);
                m->m_art_cv.wait(lk, [m] {
                    return m->m_art_quit || !m->m_art_q.empty() ||
                           (!m->m_art_warm.empty() && !m->m_art_hold);
                });
                if (m->m_art_quit) return;
                if (!m->m_art_q.empty()) {
                    j = m->m_art_q.back();          // newest first: what is on screen now
                    m->m_art_q.pop_back();
                } else {
                    warm_id = m->m_art_warm.back();
                    m->m_art_warm.pop_back();
                }
            }
            if (warm_id) {
                bool built;
                for (int kind = Art_Cover; kind <= Art_Hero; kind++)
                    LoadArt(kind, warm_id, true, built);
                continue;
            }
            j.surf = LoadArt(j.kind, j.id, false, j.built);
            std::lock_guard<std::mutex> lk(m->m_art_mx);
            m->m_art_done.push_back(j);
        }
    }
    void Menu::PollArt() {
        std::vector<ArtJob> done;
        {
            std::lock_guard<std::mutex> lk(m_art_mx);
            if (m_art_done.empty()) return;
            // A few uploads a frame: each is a copy of a few hundred KB into
            // the GPU, cheap alone but not ten at once.
            const size_t n = std::min<size_t>(m_art_done.size(),
                                              (size_t)std::max(m_cover_budget, 0) / 2);
            done.assign(m_art_done.begin(), m_art_done.begin() + n);
            m_art_done.erase(m_art_done.begin(), m_art_done.begin() + n);
        }
        for (ArtJob &j : done) {
            m_art_pending.erase({j.kind, j.id});
            // The art on the card changed while this was in flight (a fetch or
            // the picker landed): drop it, and the next frame asks again.
            if (j.epoch != m_art_epoch) { if (j.surf) SDL_FreeSurface(j.surf); continue; }
            auto &map = j.kind == Art_Cover ? m_covers
                      : j.kind == Art_Wrap  ? m_game_wraps : m_hero_art;
            SDL_Texture *tex = nullptr;
            if (j.surf) {
                tex = SDL_CreateTextureFromSurface(m_gfx->Renderer(), j.surf);
                SDL_FreeSurface(j.surf);
            }
            auto old = map.find(j.id);
            if (old != map.end() && old->second) m_gfx->FreeImage(old->second);
            map[j.id] = tex;
            if (j.kind == Art_Cover) (j.built ? g_cover_miss : g_cover_hits)++;
            if (j.built) { m_cache_msg_tick = armGetSystemTick(); m_cache_built++; }
        }
    }
    void Menu::StopArt() {
        if (!m_art_started) return;
        {
            std::lock_guard<std::mutex> lk(m_art_mx);
            m_art_quit = true;
            m_art_q.clear();
            m_art_warm.clear();
        }
        m_art_cv.notify_one();
        threadWaitForExit(&m_art_thread);
        threadClose(&m_art_thread);
        m_art_started = false;
        for (ArtJob &j : m_art_done)
            if (j.surf) SDL_FreeSurface(j.surf);
        m_art_done.clear();
        m_art_pending.clear();
    }
    bool Menu::TdbFront(u64 app_id) {
        auto f = m_tdb_front.find(app_id);
        if (f != m_tdb_front.end()) return f->second;
        char path[96];
        snprintf(path, sizeof(path), "sdmc:/slaunch/covers/%016llX_tdb", (unsigned long long)app_id);
        struct stat st {};
        return m_tdb_front[app_id] = (stat(path, &st) == 0);
    }

    // Shelf and Deck show box art too: once the selection rests on a game
    // whose cover was looked for and not found, fetch it the way Flow does.
    void Menu::FetchArtFor(const MenuItem &it) {
        if (it.kind != ItemKind::Game || it.app_id == 0) return;
        const auto c = m_covers.find(it.app_id);
        if (c != m_covers.end() && c->second == nullptr) StartCoverFetch(it.app_id, it.name);
    }

    // A small bar above the hints while box art is being fetched: which game,
    // what step, how far a download has got. Once the worker finishes, the
    // outcome stays up for a couple of seconds and fades.
    void Menu::DrawFetchStatus() {
        const auto stage = (FetchStage)m_fetch_stage.load();
        if (stage == FetchStage::Idle) return;
        float a = 1.0f;
        const bool done = stage >= FetchStage::Added;
        if (done) {
            const u64 ms = (armGetSystemTick() - m_fetch_end_tick) * 1000 / armGetSystemTickFreq();
            if (ms > 3000) { m_fetch_stage.store((int)FetchStage::Idle); return; }
            if (ms > 2400) a = 1.0f - (ms - 2400) / 600.0f;
        }
        const Theme &t = m_theme.Current();
        static const char *kText[] = { "", "Looking up", "Updating the game list", "Downloading box art",
                                       "Saving", "SteamGridDB", "Screenshots",
                                       "Box art added", "No box art found", "Could not download" };
        const Uint8 A = (Uint8)(255 * a);
        // In the middle of the top bar, which every main layout leaves free:
        // one line - step, percentage, game - with a thin bar under it.
        const int W = gfx::Gfx::Width, w = 460, x = (W - w) / 2, y = 14;
        std::string line = std::string(T(kText[(int)stage]));
        const u64 now = m_fetch_now.load(), total = m_fetch_total.load();
        if (!done && total > 0) {
            char pct[16];
            snprintf(pct, sizeof(pct), " %d%%", (int)(now * 100 / total));
            line += pct;
        }
        line += "  \xC2\xB7  " + m_fetch_title;
        m_gfx->TextCentered(FontSize::Small, W / 2, y, WithAlpha(t.dim, A),
                            Ellipsize(line, w, FontSize::Small).c_str());
        // Progress: real when a download reports its size, otherwise a
        // sliding block so it still reads as working.
        if (done) return;
        const int bx = x, by = y + m_gfx->LineHeight(FontSize::Small) + 4, bw = w;
        m_gfx->FillRect(bx, by, bw, 2, WithAlpha(t.dim, (Uint8)(60 * a)));
        if (total > 0) {
            m_gfx->FillRect(bx, by, (int)(bw * std::min(1.0, (double)now / total)), 2, WithAlpha(t.accent, A));
        } else {
            const float ph = fmodf((float)armGetSystemTick() / armGetSystemTickFreq() * 0.8f, 1.0f);
            const int seg = bw / 4, sx = bx + (int)((bw + seg) * ph) - seg;
            const int x0 = std::max(bx, sx), x1 = std::min(bx + bw, sx + seg);
            if (x1 > x0) m_gfx->FillRect(x0, by, x1 - x0, 2, WithAlpha(t.accent, A));
        }
    }

    // ---- GameTDB box scans --------------------------------------------------
    //
    // GameTDB keeps scans of the whole printed insert of almost every boxed
    // Switch game - back, spine and front in one image - with no key needed.
    // It knows games by its own five-letter ids, not by title id, so the link
    // is the name, matched strictly (net::TitlesMatch) against its title list.
    // The scan is kept downscaled as covers/<id>_wrap.png, and its front is
    // cropped out as the cover when the game has none yet.
    namespace {
        constexpr const char *kTdbIndex = "sdmc:/slaunch/cache/gametdb_switch.txt";

        // Written beside and renamed in, like net::Download, so the art worker
        // never decodes a half-written file.
        bool SavePngWhole(SDL_Surface *surf, const char *path) {
            const std::string part = std::string(path) + ".part";
            if (IMG_SavePNG(surf, part.c_str()) != 0) { remove(part.c_str()); return false; }
            remove(path);                          // FAT rename will not overwrite
            if (rename(part.c_str(), path) == 0) return true;
            remove(part.c_str());
            return false;
        }
        // Where a scan's panels sit across its width.
        constexpr float kScanSpine0 = 0.476f, kScanSpine1 = 0.524f;

        std::vector<std::string> TdbFind(const std::string &name) {
            std::vector<std::string> ids;
            FILE *fp = fopen(kTdbIndex, "r");
            if (!fp) return ids;
            char line[512];
            while (fgets(line, sizeof(line), fp) && ids.size() < 4) {
                char *eq = strstr(line, " = ");
                if (!eq || eq - line != 5) continue;           // also skips the header
                *eq = '\0';
                std::string title = eq + 3;
                while (!title.empty() && (title.back() == '\n' || title.back() == '\r'))
                    title.pop_back();
                if (net::TitlesMatch(title, name)) ids.emplace_back(line);
            }
            fclose(fp);
            return ids;
        }

        // Returns true when a wrap was written; `cover_made` when the front
        // was also saved as the game's cover.
        bool FetchGameTdb(u64 app_id, const std::string &name, bool need_cover,
                          int region, bool &cover_made, std::atomic<int> &stage,
                          std::atomic<uint64_t> &dl_now, std::atomic<uint64_t> &dl_total) {
            using Stage = FetchStage;
            cover_made = false;
            static bool s_tdb_failed = false;
            struct stat st {};
            const time_t now = time(nullptr);
            if (!s_tdb_failed && (stat(kTdbIndex, &st) != 0 || (now > st.st_mtime && now - st.st_mtime > 14 * 86400))) {
                mkdir("sdmc:/slaunch/cache", 0777);
                stage.store((int)Stage::Index);
                if (!net::Download("https://www.gametdb.com/switchtdb.txt?LANG=EN", kTdbIndex, 15, &dl_now, &dl_total)) {
                    s_tdb_failed = true;
                }
                stage.store((int)Stage::Lookup);
            }
            const std::vector<std::string> ids = TdbFind(name);
            if (ids.empty()) return false;

            std::vector<std::string> regions{ kTdbRegions[std::clamp(region, 0, kTdbRegionCount - 1)] };
            for (const char *r : { "US", "EN", "JA" })
                if (regions[0] != r) regions.emplace_back(r);

            const char *tmp = "sdmc:/slaunch/cache/gametdb_dl.jpg";
            bool got = false;
            for (const auto &id : ids) {
                for (const auto &r : regions) {
                    const std::string url = "https://art.gametdb.com/switch/coverfullHQ/" + r + "/" + id + ".jpg";
                    stage.store((int)Stage::Scan);
                    if (net::Download(url.c_str(), tmp, 40, &dl_now, &dl_total)) { got = true; break; }
                }
                if (got) break;
            }
            if (!got) return false;

            stage.store((int)Stage::Save);
            SDL_Surface *raw = IMG_Load(tmp);
            remove(tmp);
            if (!raw) return false;
            SDL_Surface *src = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGB24, 0);
            SDL_FreeSurface(raw);
            if (!src || src->w < 100 || src->h < 100) { if (src) SDL_FreeSurface(src); return false; }

            auto scaled = [&](const SDL_Rect *from, int w, int h) -> SDL_Surface * {
                SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
                if (d) SDL_BlitScaled(src, from, d, nullptr);
                return d;
            };
            char path[96];
            bool ok = false;
            const int ww = std::min(src->w, 1400), wh = src->h * ww / src->w;
            if (SDL_Surface *w = scaled(nullptr, ww, wh)) {
                // PNG, never IMG_SaveJPG: libjpeg's compressor errors on the
                // console, and SDL_image's escape from a libjpeg error takes
                // the whole menu down (see ImageLooksWhole in Http.cpp).
                snprintf(path, sizeof(path), "sdmc:/slaunch/covers/%016llX_wrap.png", (unsigned long long)app_id);
                ok = SavePngWhole(w, path);
                SDL_FreeSurface(w);
            }
            if (ok && need_cover) {
                const int fx = (int)(src->w * kScanSpine1);
                const SDL_Rect front{ fx, 0, src->w - fx, src->h };
                if (SDL_Surface *c = scaled(&front, 600, 900)) {
                    // Named .jpg because that is where every layout looks for a
                    // cover; the loader goes by the file's contents, not its name.
                    snprintf(path, sizeof(path), "sdmc:/slaunch/covers/%016llX.jpg", (unsigned long long)app_id);
                    cover_made = SavePngWhole(c, path);
                    SDL_FreeSurface(c);
                    if (cover_made) {
                        snprintf(path, sizeof(path), "sdmc:/slaunch/covers/%016llX_tdb", (unsigned long long)app_id);
                        if (FILE *f = fopen(path, "w")) fclose(f);
                    }
                }
            }
            SDL_FreeSurface(src);
            return ok;
        }
    }

    // ---- SteamGridDB cover fetch --------------------------------------------
    //
    // Switch control data carries a square icon and nothing else, so box art has
    // to come from outside. SteamGridDB's grids are 600x900 - the same 2:3 as a
    // case front - which is why it fits here.
    //
    // Two requests per title: search the name to get a game id, then ask for that
    // game's grids and download the first. Matching is by name because a title id
    // means nothing to them, so this will occasionally pick the wrong edition of
    // something; the file it writes is an ordinary cover in covers/, so a bad
    // match is fixed by replacing that one file.
    //
    // Deliberately one title at a time and only for whatever the cursor rests on:
    // walking a whole library at boot would be hundreds of requests, and this way
    // the covers you actually look at arrive first.
    void Menu::CoverFetchTrampoline(void *self) {
        Menu *m = static_cast<Menu *>(self);
        // The widget worker normally does this, but a cover fetch can start
        // before that has finished; curl_global_init is a no-op once done.
        net::GlobalInit();
        m->m_cover_ok = false;
        m->m_shots_ok = false;
        m->m_hero_ok  = false;

        // Fetch only what is actually absent.
        //
        // The screenshots used to be fetched inside the cover fetch, and the
        // cover fetch only ran when the COVER was missing - so a title that
        // already had its cover never got screenshots at all, however long you
        // sat on it. The two are looked for independently now. Hero art is a
        // third, same treatment.
        bool need_cover, need_shots, need_hero;
        {
            char probe[96];
            struct stat st {};
            snprintf(probe, sizeof(probe), "sdmc:/slaunch/covers/%016llX.jpg",
                     (unsigned long long)m->m_cover_id);
            need_cover = (stat(probe, &st) != 0);
            snprintf(probe, sizeof(probe), "sdmc:/slaunch/covers/%016llX_s0.jpg",
                     (unsigned long long)m->m_cover_id);
            need_shots = (stat(probe, &st) != 0);
            snprintf(probe, sizeof(probe), "sdmc:/slaunch/covers/%016llX_hero.jpg",
                     (unsigned long long)m->m_cover_id);
            need_hero = (stat(probe, &st) != 0);
        }
        m->m_cover_state.store((int)CoverState::Searching, std::memory_order_release);
        CoverState end = CoverState::Failed;

        // GameTDB first: keyless, and a real box scan beats everything else.
        // With one, the back of the case is printed already, so Steam's
        // screenshots are not needed either.
        m->m_wrap_ok = false;
        {
            char probe[96];
            struct stat st {};
            snprintf(probe, sizeof(probe), "sdmc:/slaunch/covers/%016llX_wrap.png",
                     (unsigned long long)m->m_cover_id);
            bool have_wrap = (stat(probe, &st) == 0);
            if (!have_wrap && !net::ContentFilter::ShouldFilterGameByName(m->m_cover_name)) {
                bool cover_made = false;
                have_wrap = FetchGameTdb(m->m_cover_id, m->m_cover_name, need_cover,
                                         m->m_tdb_region, cover_made, m->m_fetch_stage,
                                         m->m_fetch_now, m->m_fetch_total);
                m->m_wrap_ok = have_wrap;
                if (cover_made) { m->m_cover_ok = true; need_cover = false; end = CoverState::Got; }
            }
            if (have_wrap) need_shots = false;
        }
        // SteamGridDB only when the user turned it on and gave it a key;
        // otherwise only the keyless Steam screenshots remain to look for.
        if (!m->m_sgdb_enabled || m->m_sgdb_key.empty()) { need_cover = false; need_hero = false; }
        if (need_cover || need_hero) m->m_fetch_stage.store((int)FetchStage::Sgdb);

        do {
            const std::string auth = "Bearer " + m->m_sgdb_key;

            // URL-encode the title for the search path.
            std::string clean_title;
            for (size_t i = 0; i < m->m_cover_name.size(); i++) {
                unsigned char c = (unsigned char)m->m_cover_name[i];
                if (c == 0xE2 && i + 2 < m->m_cover_name.size() &&
                    (unsigned char)m->m_cover_name[i+1] == 0x84 && (unsigned char)m->m_cover_name[i+2] == 0xA2) { // ™
                    i += 2; continue;
                }
                if (c == 0xC2 && i + 1 < m->m_cover_name.size() &&
                    ((unsigned char)m->m_cover_name[i+1] == 0xAE || (unsigned char)m->m_cover_name[i+1] == 0xA9)) { // ®, ©
                    i += 1; continue;
                }
                if (c == '\r' || c == '\n' || c == '\t') { clean_title += ' '; continue; }
                clean_title += (char)c;
            }
            while (true) {
                size_t b1 = clean_title.find('[');
                size_t b2 = clean_title.find(']');
                if (b1 != std::string::npos && b2 != std::string::npos && b2 > b1) clean_title.erase(b1, b2 - b1 + 1);
                else break;
            }
            while (!clean_title.empty() && (clean_title.front() == ' ' || clean_title.front() == '\t')) clean_title.erase(clean_title.begin());
            while (!clean_title.empty() && (clean_title.back() == ' ' || clean_title.back() == '\t')) clean_title.pop_back();

            auto urlEncode = [](const std::string &str) -> std::string {
                std::string out;
                for (unsigned char c : str) {
                    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
                    else if (c == ' ') out += "%20";
                    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); out += b; }
                }
                return out;
            };

            std::string q = urlEncode(clean_title.empty() ? m->m_cover_name : clean_title);
            if (q.empty()) break;

            // Log every request with its codes. "Fetch failed" on its own
            // cannot distinguish a rejected key from an unreachable host, and
            // this is the only way to see which it is on a console.
            auto logline = [&](const char *what, long http, int rc, size_t len) {
                if (FILE *lf = fopen("sdmc:/slaunch/covers.log", "a")) {
                    fprintf(lf, "%s http=%ld curl=%d bytes=%u name=%s\n",
                            what, http, rc, (unsigned)len, m->m_cover_name.c_str());
                    fclose(lf);
                }
            };

            // Check if this game should be filtered due to adult content
            if (net::ContentFilter::ShouldFilterGameByName(m->m_cover_name)) {
                end = CoverState::Filtered;
                logline("filtered", 0, 0, m->m_cover_name.size());
                break;
            }

            std::string body;
            long http = 0; int rc = 0;
            char dst[96];

            if (!need_cover && !need_hero) {
                end = CoverState::Got;      // already on the card
                goto shots;
            }

            {
            auto parseGid = [](const std::string &b) -> long long {
                const size_t d = b.find("\"data\"");
                if (d == std::string::npos) return 0;
                const size_t idp = b.find("\"id\"", d);
                if (idp == std::string::npos) return 0;
                const size_t colon = b.find(':', idp);
                if (colon == std::string::npos) return 0;
                return strtoll(b.c_str() + colon + 1, nullptr, 10);
            };

            long long gid = 0;
            std::string url = "https://www.steamgriddb.com/api/v2/search/autocomplete/" + q;
            const bool okq = net::Get(url.c_str(), body, 10, auth.c_str(), &http, &rc);
            logline("search", http, rc, body.size());
            if (okq) {
                gid = parseGid(body);
            }

            // Fallback: search BaseTitle if colon or dash is present and full title wasn't found
            if (gid <= 0) {
                size_t cut = clean_title.find(':');
                const size_t dash = clean_title.find(" - ");
                if (dash != std::string::npos && dash < cut) cut = dash;
                if (cut != std::string::npos) {
                    std::string base = clean_title.substr(0, cut);
                    while (!base.empty() && base.back() == ' ') base.pop_back();
                    std::string q_base = urlEncode(base);
                    if (!q_base.empty() && q_base != q) {
                        url = "https://www.steamgriddb.com/api/v2/search/autocomplete/" + q_base;
                        body.clear();
                        if (net::Get(url.c_str(), body, 10, auth.c_str(), &http, &rc)) {
                            gid = parseGid(body);
                            logline("search-base", http, rc, body.size());
                        }
                    }
                }
            }

            if (gid <= 0) {
                end = (http == 401 || http == 403) ? CoverState::BadKey : CoverState::NoMatch;
                break;
            }

            if (need_cover) {
                char grid[192];
                snprintf(grid, sizeof(grid),
                         "https://www.steamgriddb.com/api/v2/grids/game/%lld"
                         "?dimensions=600x900,342x482&types=static,animated&limit=1", gid);
                body.clear();
                end = CoverState::NoArt;
                http = 0; rc = 0;
                bool okg = net::Get(grid, body, 12, auth.c_str(), &http, &rc);
                logline("grids", http, rc, body.size());
                std::string img = okg ? JsonStr(body, "url") : "";
                std::string thumb = okg ? JsonStr(body, "thumb") : "";

                // Fallback without dimension restrictions so all games show all covers
                if (img.empty()) {
                    snprintf(grid, sizeof(grid),
                             "https://www.steamgriddb.com/api/v2/grids/game/%lld"
                             "?types=static,animated&limit=1", gid);
                    body.clear();
                    http = 0; rc = 0;
                    if (net::Get(grid, body, 12, auth.c_str(), &http, &rc)) {
                        logline("grids-fallback", http, rc, body.size());
                        img = JsonStr(body, "url");
                        thumb = JsonStr(body, "thumb");
                    }
                }

                if (img.empty()) break;

                mkdir("sdmc:/slaunch/covers", 0777);
                const unsigned long long aid = (unsigned long long)m->m_cover_id;
                const bool is_vid = (img.size() >= 4 && (img.rfind(".mp4") != std::string::npos));

                if (is_vid) {
                    mkdir("sdmc:/slaunch/animated_icons", 0777);
                    snprintf(dst, sizeof(dst), "sdmc:/slaunch/animated_icons/%016llX.mp4", aid);
                    m->m_cover_ok = net::Download(img.c_str(), dst, 30);
                    snprintf(dst, sizeof(dst), "sdmc:/slaunch/covers/%016llX.mp4", aid);
                    net::Download(img.c_str(), dst, 30);
                    if (!thumb.empty()) {
                        snprintf(dst, sizeof(dst), "sdmc:/slaunch/covers/%016llX.jpg", aid);
                        net::Download(thumb.c_str(), dst, 20);
                    }
                } else {
                    snprintf(dst, sizeof(dst), "sdmc:/slaunch/covers/%016llX.jpg", aid);
                    m->m_cover_ok = net::Download(img.c_str(), dst, 25);
                }
                logline(m->m_cover_ok ? "download-ok" : "download-fail", 0, 0, img.size());
                end = m->m_cover_ok ? CoverState::Got : CoverState::Failed;
                if (!m->m_cover_ok) break;
            } else {
                end = CoverState::Got;      // cover already on the card
            }

            // Hero art for Deck's wide tile - wide key art, deliberately not
            // gated on the cover fetch above: a title that already has its
            // cover but not yet a hero still needs this to run, and a hero
            // miss must never turn a cover success into a reported failure,
            // hence its own m_hero_ok rather than folding into `end`.
            if (need_hero) {
                char hero[192];
                snprintf(hero, sizeof(hero),
                         "https://www.steamgriddb.com/api/v2/heroes/game/%lld"
                         "?dimensions=1920x620&types=static&limit=1", gid);
                body.clear();
                http = 0; rc = 0;
                bool okh = net::Get(hero, body, 12, auth.c_str(), &http, &rc);
                logline("heroes", http, rc, body.size());
                std::string himg = okh ? JsonStr(body, "url") : "";
                if (himg.empty()) {
                    snprintf(hero, sizeof(hero),
                             "https://www.steamgriddb.com/api/v2/heroes/game/%lld"
                             "?types=static&limit=1", gid);
                    body.clear();
                    http = 0; rc = 0;
                    if (net::Get(hero, body, 12, auth.c_str(), &http, &rc)) {
                        himg = JsonStr(body, "url");
                    }
                }
                if (!himg.empty()) {
                    mkdir("sdmc:/slaunch/covers", 0777);
                    snprintf(dst, sizeof(dst), "sdmc:/slaunch/covers/%016llX_hero.jpg",
                             (unsigned long long)m->m_cover_id);
                    m->m_hero_ok = net::Download(himg.c_str(), dst, 25);
                    logline(m->m_hero_ok ? "hero-download-ok" : "hero-download-fail",
                            0, 0, himg.size());
                }
            }
            }

        shots:
            if (!need_shots) break;
            m->m_fetch_stage.store((int)FetchStage::Shots);

            // The two panels on the back of the case.
            //
            // These come from Steam rather than SteamGridDB, which has no
            // screenshots at all - grids, heroes, logos and icons is its whole
            // catalogue, and its "heroes" are wide key art rather than anything
            // from the game. Steam is also keyless, and serves JPEG where a
            // hero was PNG, so the panels are far cheaper to decode.
            int shots = 0;
            if (!m->m_steam_dead) {
                // Steam, in two keyless steps: search the name for an appid,
                // then ask the store for that app's screenshots. No key, no
                // sign-up, and the shots are actual gameplay rather than the
                // wide key art SteamGridDB calls a hero.
                char url_s[288];
                snprintf(url_s, sizeof(url_s),
                         "https://steamcommunity.com/actions/SearchApps/%s", q.c_str());
                body.clear();
                http = 0; rc = 0;
                // Shorter than the SteamGridDB calls: there is a fallback
                // waiting behind this, so waiting a long time to find out it is
                // not coming only delays the panels that would have worked.
                const bool oka = net::Get(url_s, body, 8, nullptr, &http, &rc);
                logline("steam-search", http, rc, body.size());
                if (!oka && (http == 0 || http >= 500)) m->m_steam_dead = true;

                // Only a result that is really this game: Steam's search always
                // returns something, and the first hit for a title that is not
                // on Steam is some other game (see net::SteamAppFor).
                const std::string appid = oka ? net::SteamAppFor(body, m->m_cover_name) : std::string();

                if (!appid.empty()) {
                    snprintf(url_s, sizeof(url_s),
                             "https://store.steampowered.com/api/appdetails"
                             "?appids=%s&filters=screenshots", appid.c_str());
                    body.clear();
                    http = 0; rc = 0;
                    const bool okd = net::Get(url_s, body, 8, nullptr, &http, &rc);
                    logline("steam-shots", http, rc, body.size());
                    if (!okd && (http == 0 || http >= 500)) m->m_steam_dead = true;

                    if (okd) {
                        // Check if Steam store page indicates adult content
                        if (net::ContentFilter::ShouldFilterBySteamTags(body)) {
                            logline("steam-filtered", 0, 0, body.size());
                            shots = 0;  // Don't download filtered screenshots
                        } else {
                            // Each screenshot carries a thumbnail and a full-size
                            // image; the panels are drawn a few hundred pixels wide,
                            // so the full one is what is wanted.
                            const std::vector<std::string> imgs =
                                JsonStrAll(body, "path_full", 2);
                            for (size_t k = 0; k < imgs.size(); k++) {
                                snprintf(dst, sizeof(dst),
                                         "sdmc:/slaunch/covers/%016llX_s%u.jpg",
                                         (unsigned long long)m->m_cover_id, (unsigned)k);
                                const bool sok = net::Download(imgs[k].c_str(), dst, 25);
                                logline(sok ? "shot-ok" : "shot-fail", 0, 0, imgs[k].size());
                                if (sok) { shots++; m->m_shots_ok = true; }
                            }
                        }
                    }
                }
            }

            // No hero fallback. SteamGridDB's heroes are wide key art, not
            // anything from the game, and printing marketing art where a case
            // prints screenshots was only ever a stand-in for having none. A
            // title Steam has never heard of now gets a plain back rather than
            // a misleading one.
            (void)shots;
        } while (false);

        m->m_cover_state.store((int)end, std::memory_order_release);
        // The outcome, for the status bar: something new landed, nothing was
        // found, or the network let us down.
        m->m_fetch_stage.store((int)(m->m_cover_ok || m->m_wrap_ok || m->m_shots_ok || m->m_hero_ok
                                     ? FetchStage::Added
                                     : (end == CoverState::Failed ? FetchStage::Failed : FetchStage::NotFound)));
        m->m_cover_done.store(true, std::memory_order_release);
    }
    void Menu::StartCoverFetch(u64 app_id, const std::string &name) {
        if (m_cover_running || app_id == 0 || name.empty()) return;

        // No key is no longer the end of it: GameTDB and Steam need none.
        SgdbKeyPresent();                  // loads the key, if there is one
        if (m_cover_tried.count(app_id))  return;

        m_cover_tried[app_id] = true;
        m_cover_id   = app_id;
        m_cover_name = name;
        m_fetch_title = name;
        m_fetch_now.store(0); m_fetch_total.store(0);
        m_fetch_stage.store((int)FetchStage::Lookup);
        m_cover_done.store(false, std::memory_order_release);
        if (R_SUCCEEDED(threadCreate(&m_cover_thread, &Menu::CoverFetchTrampoline,
                                     this, nullptr, 0x20000, 0x3B, -2))) {
            threadStart(&m_cover_thread);
            m_cover_running = true;
        }
    }
    void Menu::PollCoverFetch() {
        if (!m_cover_running || !m_cover_done.load(std::memory_order_acquire)) return;
        threadWaitForExit(&m_cover_thread);
        threadClose(&m_cover_thread);
        m_cover_running = false;
        m_fetch_end_tick = armGetSystemTick();   // the outcome shows for a moment

        // Screenshots arrived: drop the recorded "none" so they decode.
        if (m_shots_ok) {
            auto g = m_shots.find(m_cover_id);
            if (g != m_shots.end()) {
                if (g->second.a) m_gfx->FreeImage(g->second.a);
                if (g->second.b) m_gfx->FreeImage(g->second.b);
                m_shots.erase(g);
            }
        }

        if (m_wrap_ok || m_hero_ok || m_cover_ok) m_art_epoch++;
        // A box scan arrived: drop what was cached for this game so the wrap
        // (and a cover cut from it) are picked up.
        if (m_wrap_ok) {
            auto w = m_game_wraps.find(m_cover_id);
            if (w != m_game_wraps.end()) {
                if (w->second) m_gfx->FreeImage(w->second);
                m_game_wraps.erase(w);
            }
            m_tdb_front.erase(m_cover_id);
        }

        // Hero art arrived: same, for HeroArt's own cache.
        if (m_hero_ok) {
            auto h = m_hero_art.find(m_cover_id);
            if (h != m_hero_art.end()) {
                if (h->second) m_gfx->FreeImage(h->second);
                m_hero_art.erase(h);
            }
        }

        // Drop the cached miss so FlowCover picks the new file up next frame.
        if (m_cover_ok) {
            m_cover_ok_count++;
            auto f = m_covers.find(m_cover_id);
            if (f != m_covers.end()) {
                if (f->second) m_gfx->FreeImage(f->second);
                m_covers.erase(f);
            }
            auto g = m_shots.find(m_cover_id);
            if (g != m_shots.end()) {
                if (g->second.a) m_gfx->FreeImage(g->second.a);
                if (g->second.b) m_gfx->FreeImage(g->second.b);
                m_shots.erase(g);
            }
        }
    }
    // Decoded on a worker; see the note on m_shot_thread. Returns nothing until
    // the panels are ready, so the back of a case is briefly blank rather than
    // the whole menu stopping to inflate two 1920x620 images.
    const Menu::FlowShots &Menu::FlowBackShots(const MenuItem &it) {
        static const FlowShots none;
        if (it.kind != ItemKind::Game || it.app_id == 0) return none;

        auto f = m_shots.find(it.app_id);
        if (f != m_shots.end()) return f->second;

        // One at a time: two of these already saturate a core, and only the box
        // you have stopped on ever asks.
        if (!m_shot_running) StartShotDecode(it.app_id);
        return none;
    }
    void Menu::StartShotDecode(u64 app_id) {
        snprintf(m_shot_path_a, sizeof(m_shot_path_a),
                 "sdmc:/slaunch/covers/%016llX_s0.jpg", (unsigned long long)app_id);
        snprintf(m_shot_path_b, sizeof(m_shot_path_b),
                 "sdmc:/slaunch/covers/%016llX_s1.jpg", (unsigned long long)app_id);
        m_shot_job_id = app_id;
        m_shot_surf_a = nullptr;
        m_shot_surf_b = nullptr;
        m_shot_done.store(false, std::memory_order_release);
        if (R_SUCCEEDED(threadCreate(&m_shot_thread, &Menu::ShotDecodeTrampoline,
                                     this, nullptr, 0x20000, 0x3B, -2))) {
            threadStart(&m_shot_thread);
            m_shot_running = true;
        } else {
            // No worker available: record the miss so it is not retried every
            // frame for the rest of the session.
            m_shots[app_id] = FlowShots{};
            m_shot_job_id = 0;
        }
    }
    // Surfaces only - IMG_Load, a format convert and a scaled blit are all CPU
    // work on ordinary memory. Nothing here touches the renderer, which is what
    // makes it safe off the main thread.
    void Menu::ShotDecodeTrampoline(void *self) {
        Menu *m = static_cast<Menu *>(self);
        m->m_shot_surf_a = DecodeCoverSurface(m->m_shot_path_a, kShotTexW, kShotTexH);
        m->m_shot_surf_b = DecodeCoverSurface(m->m_shot_path_b, kShotTexW, kShotTexH);
        m->m_shot_done.store(true, std::memory_order_release);
    }
    void Menu::PollShotDecode() {
        if (!m_shot_running || !m_shot_done.load(std::memory_order_acquire)) return;
        threadWaitForExit(&m_shot_thread);
        threadClose(&m_shot_thread);
        m_shot_running = false;

        FlowShots s;
        if (m_shot_surf_a) {
            s.a = SDL_CreateTextureFromSurface(m_gfx->Renderer(), m_shot_surf_a);
            SDL_FreeSurface(m_shot_surf_a);
            m_shot_surf_a = nullptr;
        }
        if (m_shot_surf_b) {
            s.b = SDL_CreateTextureFromSurface(m_gfx->Renderer(), m_shot_surf_b);
            SDL_FreeSurface(m_shot_surf_b);
            m_shot_surf_b = nullptr;
        }
        // Recorded even when both are null, so a title with no hero art is not
        // asked for again every frame.
        if (m_shot_job_id != 0) m_shots[m_shot_job_id] = s;
        m_shot_job_id = 0;
    }
    Menu::Screen Menu::BackTarget() {
        if (m_from_deck_menu) { m_from_deck_menu = false; return Screen::DeckMenu; }
        return Screen::Main;
    }
} // namespace sl::menu::ui
