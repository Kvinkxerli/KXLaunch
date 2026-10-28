#pragma once
#include <string>
#include <vector>

// Adult content filtering for news and art fetching.
//
// Blocks content based on:
//   - Game ratings (ESRB M/AO, PEGI 16/18, USK 16/18, etc.)
//   - Content tags from Steam, SteamGridDB, and news sources
//   - Metadata in news articles and cover art descriptions
//
// Filtering can be enabled/disabled via config file:
//   sdmc:/slaunch/config/content_filter.txt -> "enabled=1" (or 0)

namespace sl::menu::net {

    class ContentFilter {
        public:
            // Check if the content filter is enabled in config.
            static bool IsEnabled();

            // Save the enabled/disabled state to config.
            static void SetEnabled(bool enabled);

            // Check if a game title should be filtered (based on name/known ratings).
            static bool ShouldFilterGameByName(const std::string &title);

            // Check if ESRB rating string should be filtered.
            // Examples: "Mature (M)", "Adults Only (AO)", "Teen (T)", etc.
            static bool ShouldFilterByEsrb(const std::string &esrb_rating);

            // Check if PEGI rating should be filtered.
            // Examples: "PEGI 16", "PEGI 18", "PEGI 12", etc.
            static bool ShouldFilterByPegi(const std::string &pegi_rating);

            // Check if USK rating should be filtered.
            // Examples: "16", "18", "6", etc.
            static bool ShouldFilterByUsk(const std::string &usk_rating);

            // Check if content tags suggest adult content.
            // Tags like "sexual content", "violence", "drugs", etc.
            static bool ShouldFilterByTags(const std::vector<std::string> &tags);

            // Parse tags from a comma/space-separated string.
            static std::vector<std::string> ParseTags(const std::string &tags_str);

            // Check Steam store page tags (from API response).
            static bool ShouldFilterBySteamTags(const std::string &json_body);

            // Check if news article content indicates adult material.
            static bool ShouldFilterNewsArticle(const std::string &title,
                                                 const std::string &summary,
                                                 const std::string &category);

            // Check if Steam news post should be filtered.
            static bool ShouldFilterSteamNews(const std::string &title,
                                              const std::string &content);

            // Check if art/grid metadata indicates adult content.
            static bool ShouldFilterArtMetadata(const std::string &json_body,
                                                 const std::string &source);
    };

} // namespace sl::menu::net
