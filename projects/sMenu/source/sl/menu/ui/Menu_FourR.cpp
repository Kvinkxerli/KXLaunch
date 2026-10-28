#include <sl/menu/ui/Menu.hpp>
#include <sl/menu/ui/Locale.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include "Menu_Internal.hpp"

namespace sl::menu::ui {

    static bool FileExists(const std::string &path) {
        struct stat st;
        return (stat(path.c_str(), &st) == 0);
    }

    std::string Menu::ResolveGameVideoPath(const MenuItem &it) {
        if (it.kind == ItemKind::Game && it.app_id != 0) {
            char path_buf[160];
            const unsigned long long aid = (unsigned long long)it.app_id;
            // FAT32/exFAT is case-insensitive: %016llX covers both cases, cutting stat() calls in half
            static const char *const kVideoFormats[] = {
                "sdmc:/slaunch/backgrounds/animated/%016llX.mp4",
                "sdmc:/slaunch/backgrounds/animated/%016llX.webp",
                "sdmc:/kxlaunch/backgrounds/animated/%016llX.mp4",
                "sdmc:/kxlaunch/backgrounds/animated/%016llX.webp",
                "sdmc:/slaunch/backgrounds/video/%016llX.mp4",
                "sdmc:/slaunch/backgrounds/%016llX.mp4",
                "sdmc:/recordings/backgrounds/%016llX.mp4",
            };
            for (const char *fmt : kVideoFormats) {
                snprintf(path_buf, sizeof(path_buf), fmt, aid);
                if (FileExists(path_buf)) return path_buf;
            }
        } else if (it.kind == ItemKind::Homebrew) {
            std::string base = it.hb_path;
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);

            if (!base.empty()) {
                char path_buf[160];
                static const char *const kHbVideoFormats[] = {
                    "sdmc:/slaunch/backgrounds/animated/%s.mp4",
                    "sdmc:/slaunch/backgrounds/animated/%s.webp",
                    "sdmc:/kxlaunch/backgrounds/animated/%s.mp4",
                    "sdmc:/kxlaunch/backgrounds/animated/%s.webp",
                    "sdmc:/slaunch/backgrounds/video/%s.mp4",
                    "sdmc:/slaunch/backgrounds/%s.mp4",
                    "sdmc:/recordings/backgrounds/%s.mp4",
                };
                for (const char *fmt : kHbVideoFormats) {
                    snprintf(path_buf, sizeof(path_buf), fmt, base.c_str());
                    if (FileExists(path_buf)) return path_buf;
                }
            }
        }
        return "";
    }

    std::string Menu::ResolveGameImagePath(const MenuItem &it) {
        if (it.kind == ItemKind::Game && it.app_id != 0) {
            char path_buf[160];
            const unsigned long long aid = (unsigned long long)it.app_id;
            const char *exts[] = { ".jpg", ".png", ".jpeg", ".webp" };
            for (const char *ext : exts) {
                snprintf(path_buf, sizeof(path_buf), "sdmc:/slaunch/backgrounds/static/%016llX%s", aid, ext);
                if (FileExists(path_buf)) return path_buf;
                snprintf(path_buf, sizeof(path_buf), "sdmc:/kxlaunch/backgrounds/static/%016llX%s", aid, ext);
                if (FileExists(path_buf)) return path_buf;
                snprintf(path_buf, sizeof(path_buf), "sdmc:/slaunch/backgrounds/%016llX%s", aid, ext);
                if (FileExists(path_buf)) return path_buf;
                snprintf(path_buf, sizeof(path_buf), "sdmc:/kxlaunch/backgrounds/%016llX%s", aid, ext);
                if (FileExists(path_buf)) return path_buf;
                snprintf(path_buf, sizeof(path_buf), "sdmc:/recordings/backgrounds/%016llX%s", aid, ext);
                if (FileExists(path_buf)) return path_buf;
            }
        } else if (it.kind == ItemKind::Homebrew) {
            std::string base = it.hb_path;
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            if (!base.empty()) {
                char path_buf[160];
                const char *exts[] = { ".jpg", ".png", ".jpeg", ".webp" };
                for (const char *ext : exts) {
                    snprintf(path_buf, sizeof(path_buf), "sdmc:/slaunch/backgrounds/static/%s%s", base.c_str(), ext);
                    if (FileExists(path_buf)) return path_buf;
                    snprintf(path_buf, sizeof(path_buf), "sdmc:/kxlaunch/backgrounds/static/%s%s", base.c_str(), ext);
                    if (FileExists(path_buf)) return path_buf;
                    snprintf(path_buf, sizeof(path_buf), "sdmc:/slaunch/backgrounds/%s%s", base.c_str(), ext);
                    if (FileExists(path_buf)) return path_buf;
                    snprintf(path_buf, sizeof(path_buf), "sdmc:/kxlaunch/backgrounds/%s%s", base.c_str(), ext);
                    if (FileExists(path_buf)) return path_buf;
                    snprintf(path_buf, sizeof(path_buf), "sdmc:/recordings/backgrounds/%s%s", base.c_str(), ext);
                    if (FileExists(path_buf)) return path_buf;
                }
            }
        }
        return "";
    }

    void Menu::DrawUnifiedBackdrop() {
        if (m_items.empty()) return;

        const int mode_idx = std::clamp((int)m_ui_mode, 0, (int)UiMode::Count - 1);
        const bool video_enabled = m_mode_video_bg[mode_idx];
        const int  dim_level     = m_mode_bg_dim[mode_idx];

        const MenuItem *sel_it = nullptr;
        if (m_ui_mode == UiMode::XMB) {
            if (m_xmb_col >= 0 && m_xmb_col < (int)m_xmb_cols.size()) {
                const auto &col_items = m_xmb_cols[m_xmb_col].items;
                if (m_xmb_item >= 0 && m_xmb_item < (int)col_items.size()) {
                    sel_it = &m_items[col_items[m_xmb_item]];
                }
            }
        }
        if (!sel_it) {
            sel_it = &m_items[std::min(m_cursor, (int)m_items.size() - 1)];
        }
        const MenuItem &it = *sel_it;
        const u64 now = armGetSystemTick(), hz = armGetSystemTickFreq();

        const u64 sel_key = (it.kind == ItemKind::Game) ? it.app_id : (it.kind == ItemKind::Homebrew ? it.hb_icon : 0);

        // Track game selection changes
        if (sel_key != m_video_target_app_id) {
            m_video_target_app_id   = sel_key;
            m_video_target_tick     = now;
            m_video_target_resolved = false;
        }

        // Debounce: wait ~200ms after user moves selection before initiating video load
        // so fast browsing through games does not trigger rapid opens/closes on NVDEC
        const bool debounce_passed = ((now - m_video_target_tick) * 1000 / hz) >= 200;

        if (debounce_passed && !m_video_target_resolved) {
            m_video_target_resolved = true;
            std::string desired_vid = video_enabled ? ResolveGameVideoPath(it) : "";
            if (desired_vid != m_video_target_path) {
                m_video_target_path = desired_vid;
                m_video_bg_player.Close();
                m_video_bg_alpha = 0.0f;

                if (!desired_vid.empty()) {
                    m_video_bg_player.Open(m_gfx, desired_vid.c_str(), true, false, true);
                }
            }
        }

        // Advance playback for the current playing video
        if (!m_video_target_path.empty()) {
            m_video_bg_player.Tick();
        }

        SDL_Texture *vtex = m_video_bg_player.GetTexture();
        const bool has_video_frame = (vtex != nullptr && m_video_bg_player.HasFrame());

        // While video is loading / absent / fading, draw the clean selection backdrop
        if (!has_video_frame || m_video_bg_alpha < 0.98f) {
            DrawSelectionBackdrop();
        }

        // Smoothly cross-fade video in once first frame is ready
        if (has_video_frame && debounce_passed) {
            m_video_bg_alpha = std::min(1.0f, m_video_bg_alpha + 0.06f);
            const float va_eased = m_video_bg_alpha * m_video_bg_alpha * (3.0f - 2.0f * m_video_bg_alpha);
            const Uint8 va = (Uint8)(va_eased * 255.0f);
            const float vzoom = 1.02f - 0.02f * va_eased;
            m_gfx->DrawCover(vtex, va, vzoom);
        } else if (vtex && !debounce_passed && m_video_bg_alpha > 0.01f) {
            // User just moved away from this game: smoothly cross-dissolve old video to backdrop!
            m_video_bg_alpha = std::max(0.0f, m_video_bg_alpha - 0.08f);
            const float va_eased = m_video_bg_alpha * m_video_bg_alpha * (3.0f - 2.0f * m_video_bg_alpha);
            m_gfx->DrawCover(vtex, (Uint8)(va_eased * 255.0f));
        } else {
            m_video_bg_alpha = 0.0f;
        }

        // Per-mode dimming overlay:
        // 0 = Off (0%), 1 = Light (~20%), 2 = Medium (~41%), 3 = Dark (~65%)
        static const Uint8 kDimAlphas[4] = { 0, 52, 105, 168 };
        const Uint8 dim_a = kDimAlphas[std::clamp(dim_level, 0, 3)];
        if (dim_a > 0) {
            m_gfx->FillRect(0, 0, gfx::Gfx::Width, gfx::Gfx::Height, { 0, 0, 0, dim_a });
        }
    }

    int Menu::FourRItemAt(int x, int y) const {
        const int last = (int)m_items.size() - 1;
        if (last < 0) return -1;
        const int row_y = 200;
        const int size = 256;
        if (y < row_y - 10 || y > row_y + size + 20) return -1;

        const int pitch = 272; // 256px tile + 16px gap
        const int cx = gfx::Gfx::Width / 2;
        const int idx = (int)lroundf(m_scroll_pos + (float)(x - cx) / (float)pitch);
        return (idx >= 0 && idx <= last) ? idx : -1;
    }

    void Menu::DrawMainFourR() {
        const Theme &t = m_theme.Current();
        m_icons.SetScale(0); // Crisp full-resolution covers

        if (m_items.empty()) {
            DrawMainEmpty();
            return;
        }

        // Smooth spring scroll easing
        if (!ScrollBusy())
            m_scroll_pos += (m_cursor - m_scroll_pos) * 0.32f;
        if (std::abs(m_cursor - m_scroll_pos) < 0.01f)
            m_scroll_pos = (float)m_cursor;

        const int W = gfx::Gfx::Width;
        const int cx = W / 2;
        const int total = (int)m_items.size();

        // 2. Top Status Bar (Authentic 4R layout)
        {
            // Left: User Profile Avatar & Date/Time
            const int avatar_x = 48;
            const int avatar_y = 20;
            const int avatar_sz = 44;

            // Lazily load avatar if it was not available during initial boot
            if (!m_user_avatar) {
                static u64 s_last_avatar_poll = 0;
                const u64 now_tick = armGetSystemTick();
                if (now_tick - s_last_avatar_poll > armGetSystemTickFreq()) {
                    s_last_avatar_poll = now_tick;
                    LoadUserAvatar();
                }
            }

            gfx::Gfx::CardStyle av_cs;
            av_cs.radius  = avatar_sz * 0.5f; // circular avatar
            av_cs.shadow  = 0.0f;
            av_cs.glow    = 0.0f;
            av_cs.fill    = { 0, 0, 0, 0 };
            av_cs.tint    = { 255, 255, 255, 255 };

            if (m_user_avatar) {
                if (!m_gfx->Card(m_user_avatar, (float)avatar_x, (float)avatar_y,
                                 (float)avatar_sz, (float)avatar_sz, av_cs, 255)) {
                    m_gfx->DrawImage(m_user_avatar, avatar_x, avatar_y, avatar_sz, avatar_sz, 255);
                }
            } else {
                m_gfx->Card(nullptr, (float)avatar_x, (float)avatar_y,
                            (float)avatar_sz, (float)avatar_sz, av_cs, 255);
                char initial[2] = { m_nickname[0] ? m_nickname[0] : 'U', '\0' };
                if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;
                m_gfx->TextCentered(FontSize::Normal, avatar_x + avatar_sz / 2, avatar_y + 11,
                                    { 255, 255, 255, 255 }, initial);
            }

            // Elegant circular outline ring framing the avatar
            const float acx = avatar_x + avatar_sz * 0.5f;
            const float acy = avatar_y + avatar_sz * 0.5f;
            const float arad = avatar_sz * 0.5f + 1.0f;
            for (int deg = 0; deg < 360; deg += 4) {
                const float rad_a = deg * 3.14159265f / 180.0f;
                const int px = (int)(acx + arad * cosf(rad_a));
                const int py = (int)(acy + arad * sinf(rad_a));
                m_gfx->FillRect(px, py, 1, 1, { 255, 255, 255, 75 });
            }

            // Clock & Date (e.g. "00:33  Thu Sep 24" as in 4R)
            time_t now_sec = time(nullptr);
            struct tm tm_now;
            localtime_r(&now_sec, &tm_now);
            static const char *const kWeekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            static const char *const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
            const char *wday = (tm_now.tm_wday >= 0 && tm_now.tm_wday < 7) ? T(kWeekdays[tm_now.tm_wday]) : "";
            const char *month = (tm_now.tm_mon >= 0 && tm_now.tm_mon < 12) ? T(kMonths[tm_now.tm_mon]) : "";

            char clock_buf[64];
            snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d   %s %s %d",
                     tm_now.tm_hour, tm_now.tm_min, wday, month, tm_now.tm_mday);
            m_gfx->Text(FontSize::Normal, avatar_x + avatar_sz + 16, avatar_y + 11,
                        { 235, 235, 245, 225 }, clock_buf);

            // Right side: Player name, Wi-Fi, Battery Meter
            int rx = W - 52;

            u32 charge = 0;
            psmGetBatteryChargePercentage(&charge);
            PsmChargerType charger = PsmChargerType_Unconnected;
            bool is_charging = false;
            if (R_SUCCEEDED(psmGetChargerType(&charger)))
                is_charging = (charger != PsmChargerType_Unconnected);

            const int bat_w = 32, bat_h = 16, bat_y = 34;
            rx -= bat_w;
            // Battery shell
            m_gfx->FillRect(rx, bat_y, bat_w, 2, { 255, 255, 255, 220 });
            m_gfx->FillRect(rx, bat_y + bat_h - 2, bat_w, 2, { 255, 255, 255, 220 });
            m_gfx->FillRect(rx, bat_y, 2, bat_h, { 255, 255, 255, 220 });
            m_gfx->FillRect(rx + bat_w - 2, bat_y, 2, bat_h, { 255, 255, 255, 220 });
            m_gfx->FillRect(rx + bat_w, bat_y + 4, 2, bat_h - 8, { 255, 255, 255, 220 });

            SDL_Color bat_col = is_charging ? SDL_Color{ 100, 245, 140, 255 }
                                : (charge > 20) ? SDL_Color{ 245, 245, 250, 255 }
                                : SDL_Color{ 255, 75, 75, 255 };
            int fill_w = (int)((bat_w - 6) * std::clamp(charge / 100.0f, 0.0f, 1.0f));
            if (fill_w > 0)
                m_gfx->FillRect(rx + 3, bat_y + 3, fill_w, bat_h - 6, bat_col);

            char batt_str[24];
            snprintf(batt_str, sizeof(batt_str), is_charging ? "+%lu%%" : "%lu%%", (unsigned long)charge);
            const int pw = m_gfx->TextWidth(FontSize::Small, batt_str);
            rx -= (pw + 10);
            m_gfx->Text(FontSize::Small, rx, bat_y - 2, bat_col, batt_str);

            // Wi-Fi signal icon
            rx -= 26;
            m_gfx->FillRect(rx,      bat_y + 9, 3, 5,  { 255, 255, 255, 220 });
            m_gfx->FillRect(rx + 5,  bat_y + 5, 3, 9,  { 255, 255, 255, 220 });
            m_gfx->FillRect(rx + 10, bat_y + 1, 3, 13, { 255, 255, 255, 220 });

            // Nickname
            const int nw = m_gfx->TextWidth(FontSize::Normal, m_nickname);
            rx -= (nw + 18);
            m_gfx->Text(FontSize::Normal, rx, 30, { 240, 240, 250, 240 }, m_nickname);
        }

        // 3. 4R Horizontal Game Row
        // Tiles with rounded corners, no reflection (as requested), and authentic glowing 4R neon frame
        const int row_y = 200;
        const int size = 256;
        const int pitch = 272; // 256px tile + 16px gap
        const int span = 4;

        auto drawTile = [&](int off) {
            int idx = (int)lroundf(m_scroll_pos) + off;
            if (idx < 0 || idx >= total) return;

            const int tile_x = cx + (int)((idx - m_scroll_pos) * pitch) - size / 2;
            const int tile_y = row_y;

            if (tile_x + size < -50 || tile_x > W + 50) return;

            const bool is_sel = (idx == m_cursor);

            // In 4R, tiles are clean, vivid and never dimmed or grayed out:
            const MenuItem &item = m_items[idx];
            const bool isGame = (item.kind == ItemKind::Game);
            const bool isHb   = (item.kind == ItemKind::Homebrew);
            SDL_Texture *icon = ItemIcon(item, is_sel);

            gfx::Gfx::CardStyle cs;
            cs.radius   = 28.0f;
            cs.shadow   = 0.0f;  // NO dark shadow: 4R icons stay 100% bright and clean!
            cs.glow     = 0.0f;  // Authentic outer bloom is rendered cleanly by DrawNeonRing below
            cs.glow_col = { 0, 229, 255, 255 }; // 4IFIR cyan
            cs.reflect  = false; // NO reflection underneath (as explicitly requested)
            if (icon && (isGame || isHb)) {
                cs.fill = { 0, 0, 0, 0 };
                cs.tint = item.is_archived ? kArchivedTint : SDL_Color{ 255, 255, 255, 255 };
            } else if (icon) {
                cs.fill  = IconPlate(t, 255);
                cs.glyph = true;
                cs.tint  = IconTint(t, 255);
            } else {
                cs.fill = WithAlpha(t.bg_bottom, 180);
                cs.tint = { 255, 255, 255, 255 };
            }

            if (!m_gfx->Card(icon, (float)tile_x, (float)tile_y, (float)size, (float)size, cs, 255)) {
                if (icon) m_gfx->DrawImage(icon, tile_x, tile_y, size, size, 255);
                else      m_gfx->FillRect(tile_x, tile_y, size, size, WithAlpha(t.bg_bottom, 180));
            }

            // Authentic 4R Neon Border (exact 4IFIR neon cyan / ice-blue with white laser core & bloom):
            if (is_sel) {
                const float pulse = 0.88f + 0.12f * sinf((float)armGetSystemTick() / (float)armGetSystemTickFreq() * 3.5f);
                m_gfx->DrawNeonRing((float)tile_x, (float)tile_y, (float)size, (float)size, 28.0f, pulse);
            }
        };

        for (int d = span; d >= 1; d--) {
            drawTile(-d);
            drawTile(+d);
        }
        drawTile(0);

        // 4. Centered Game Title & Position Below the Active Tile (Authentic 4R)
        {
            const MenuItem &sel = m_items[m_cursor];
            const bool is_run = (sel.kind == ItemKind::Game && m_suspended != 0 && sel.app_id == m_suspended);

            // Positioned cleanly below the tile
            const int title_y = row_y + size + 22;
            const std::string title_str = Ellipsize(sel.name, W - 120, FontSize::Title);

            // Centered Title in crisp white with deep drop shadow
            m_gfx->TextCentered(FontSize::Title, cx + 2, title_y + 2, { 0, 0, 0, 240 }, title_str.c_str());
            m_gfx->TextCentered(FontSize::Title, cx, title_y, { 255, 255, 255, 255 }, title_str.c_str());

            // Centered Subtitle: Generous spacing below the title (title_y + 58)
            const int sub_y = title_y + 58;
            char sub_buf[64];
            if (is_run) {
                snprintf(sub_buf, sizeof(sub_buf), "%s", T("Playing"));
                m_gfx->TextCentered(FontSize::Small, cx + 1, sub_y + 1, { 0, 0, 0, 200 }, sub_buf);
                m_gfx->TextCentered(FontSize::Small, cx, sub_y, SDL_Color{ 100, 245, 140, 255 }, sub_buf);
            } else if (sel.is_archived) {
                snprintf(sub_buf, sizeof(sub_buf), "%s", T("Archived"));
                m_gfx->TextCentered(FontSize::Small, cx + 1, sub_y + 1, { 0, 0, 0, 200 }, sub_buf);
                m_gfx->TextCentered(FontSize::Small, cx, sub_y, SDL_Color{ 180, 180, 190, 255 }, sub_buf);
            } else {
                snprintf(sub_buf, sizeof(sub_buf), "%d / %d", m_cursor + 1, total);
                m_gfx->TextCentered(FontSize::Small, cx + 1, sub_y + 1, { 0, 0, 0, 200 }, sub_buf);
                m_gfx->TextCentered(FontSize::Small, cx, sub_y, SDL_Color{ 190, 195, 210, 255 }, sub_buf);
            }
        }

        // 5. Bottom Controls (Authentic clean 4R hints - strictly without bottom game chat panel)
        {
            const MenuItem &sel = m_items[m_cursor];
            const bool is_run = (sel.kind == ItemKind::Game && m_suspended != 0 && sel.app_id == m_suspended);

            if (is_run) {
                DrawStatusHint({ {{"a"}, "Resume"}, {{"plus"}, "Options"}, {{"y"}, "Search"}, {{"minus"}, "Settings"} });
            } else {
                DrawStatusHint({ {{"a"}, "Start"}, {{"plus"}, "Options"}, {{"y"}, "Search"}, {{"minus"}, "Settings"} });
            }
        }
    }

} // namespace sl::menu::ui
