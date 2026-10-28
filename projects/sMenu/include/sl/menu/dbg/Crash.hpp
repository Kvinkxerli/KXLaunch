#pragma once

namespace sl::menu::dbg {

    // How many times in a row the menu has crashed. Bumped by the exception
    // handler (Crash.cpp), cleared by main.cpp once a run has stayed up.
    inline constexpr const char *kCrashCountPath = "sdmc:/slaunch/config/menu_crashes";
    // From this many in a row the menu starts with its extras off.
    inline constexpr int kSafeModeCrashes = 2;

}
