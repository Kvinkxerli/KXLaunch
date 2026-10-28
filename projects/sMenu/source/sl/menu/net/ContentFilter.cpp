#include <sl/menu/net/ContentFilter.hpp>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>

namespace sl::menu::net {

    namespace {
        constexpr const char *kConfigPath = "sdmc:/slaunch/config/content_filter.txt";
        static int s_cached_enabled = -1; // -1 = unread, 0 = false, 1 = true

        std::string ToLower(const std::string &s) {
            std::string result = s;
            for (auto &c : result) c = (char)std::tolower((unsigned char)c);
            return result;
        }

        bool ContainsCI(const std::string &haystack, const std::string &needle) {
            const std::string lower_hay = ToLower(haystack);
            const std::string lower_nee = ToLower(needle);
            return lower_hay.find(lower_nee) != std::string::npos;
        }

        // Checks for keyword with word boundaries so "Sussex" doesn't trigger "sex"
        bool HasWordBoundaryMatch(const std::string &text, const char *kw) {
            const size_t kw_len = strlen(kw);
            if (kw_len == 0 || text.empty()) return false;
            size_t pos = 0;
            while ((pos = text.find(kw, pos)) != std::string::npos) {
                const bool start_ok = (pos == 0) || !std::isalnum((unsigned char)text[pos - 1]);
                const bool end_ok = (pos + kw_len >= text.size()) || !std::isalnum((unsigned char)text[pos + kw_len]);
                if (start_ok && end_ok) return true;
                pos += kw_len;
            }
            return false;
        }

        bool IsAdultKeyword(const std::string &word) {
            static const char *adult_keywords[] = {
                "sexual", "adult", "porn", "xxx", "nude", "naked", "explicit",
                "erotic", "18+", "mature", "hentai", "sex", "orgasm",
                "rape", "incest", "prostitut", "fetish", "bondage", "bdsm"
            };
            const std::string lower = ToLower(word);
            for (const char *kw : adult_keywords) {
                if (HasWordBoundaryMatch(lower, kw)) return true;
            }
            return false;
        }

        bool IsExtremeViolenceKeyword(const std::string &word) {
            static const char *violence_keywords[] = {
                "extremely violent", "graphic violence", "gore", "mutilation",
                "dismember", "decapitat", "eviscerat"
            };
            const std::string lower = ToLower(word);
            for (const char *kw : violence_keywords) {
                if (lower.find(kw) != std::string::npos) return true;
            }
            return false;
        }

        bool HasAdultKeywords(const std::string &text) {
            return IsAdultKeyword(text) || IsExtremeViolenceKeyword(text);
        }
    } // namespace

    bool ContentFilter::IsEnabled() {
        if (s_cached_enabled >= 0) return s_cached_enabled != 0;

        FILE *fp = fopen(kConfigPath, "r");
        if (!fp) {
            s_cached_enabled = 0;
            return false;
        }

        char line[128];
        bool enabled = false;
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "enabled=", 8) == 0) {
                enabled = (atoi(line + 8) != 0);
                break;
            }
        }
        fclose(fp);
        s_cached_enabled = enabled ? 1 : 0;
        return enabled;
    }

    void ContentFilter::SetEnabled(bool enabled) {
        s_cached_enabled = enabled ? 1 : 0;
        mkdir("sdmc:/slaunch/config", 0777);
        FILE *fp = fopen(kConfigPath, "w");
        if (fp) {
            fprintf(fp, "enabled=%d\n", enabled ? 1 : 0);
            fclose(fp);
        }
    }

    bool ContentFilter::ShouldFilterGameByName(const std::string &title) {
        if (!IsEnabled()) return false;
        return HasAdultKeywords(title);
    }

    bool ContentFilter::ShouldFilterByEsrb(const std::string &esrb_rating) {
        if (!IsEnabled()) return false;
        const std::string lower = ToLower(esrb_rating);
        if (lower.find("adults only") != std::string::npos) return true;
        if (lower.find("ao") != std::string::npos && lower.find("18+") != std::string::npos) return true;
        return false;
    }

    bool ContentFilter::ShouldFilterByPegi(const std::string &pegi_rating) {
        if (!IsEnabled()) return false;
        const std::string lower = ToLower(pegi_rating);
        return lower.find("pegi 18") != std::string::npos;
    }

    bool ContentFilter::ShouldFilterByUsk(const std::string &usk_rating) {
        if (!IsEnabled()) return false;
        const std::string lower = ToLower(usk_rating);
        return lower.find("18") != std::string::npos;
    }

    std::vector<std::string> ContentFilter::ParseTags(const std::string &tags_str) {
        std::vector<std::string> tags;
        std::string current;
        for (char c : tags_str) {
            if (c == ',' || c == ' ' || c == ';' || c == '|') {
                if (!current.empty()) {
                    tags.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) tags.push_back(current);
        return tags;
    }

    bool ContentFilter::ShouldFilterByTags(const std::vector<std::string> &tags) {
        if (!IsEnabled()) return false;
        for (const auto &tag : tags) {
            if (HasAdultKeywords(tag)) return true;
        }
        return false;
    }

    bool ContentFilter::ShouldFilterBySteamTags(const std::string &json_body) {
        if (!IsEnabled()) return false;

        static const char *steam_adult_tags[] = {
            "sexual_content",
            "nudity",
            "mature_content",
            "explicit_sexual_content",
            "18+"
        };

        const std::string lower = ToLower(json_body);
        for (const char *tag : steam_adult_tags) {
            if (lower.find(tag) != std::string::npos) return true;
        }
        return HasAdultKeywords(json_body);
    }

    bool ContentFilter::ShouldFilterNewsArticle(const std::string &title,
                                                 const std::string &summary,
                                                 const std::string &category) {
        if (!IsEnabled()) return false;
        if (HasAdultKeywords(title) || HasAdultKeywords(summary)) return true;
        const std::string lower_cat = ToLower(category);
        return lower_cat.find("adult") != std::string::npos || lower_cat.find("mature") != std::string::npos;
    }

    bool ContentFilter::ShouldFilterSteamNews(const std::string &title,
                                              const std::string &content) {
        if (!IsEnabled()) return false;
        return HasAdultKeywords(title) || HasAdultKeywords(content);
    }

    bool ContentFilter::ShouldFilterArtMetadata(const std::string &json_body,
                                                 const std::string &source) {
        if (!IsEnabled()) return false;
        if (ContainsCI(source, "steamgriddb")) {
            if (ContainsCI(json_body, "adult") || ContainsCI(json_body, "nsfw") || ContainsCI(json_body, "explicit"))
                return true;
        } else if (ContainsCI(source, "steam")) {
            if (ShouldFilterBySteamTags(json_body)) return true;
        }
        return HasAdultKeywords(json_body);
    }

} // namespace sl::menu::net
