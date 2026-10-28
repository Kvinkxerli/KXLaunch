#pragma once
#include <switch.h>
#include <sl/sys/HbLaunchRequest.hpp>

// The daemon's end of the homebrew launch drop box (see HbLaunchRequest.hpp for
// the format and for why it is a file rather than a service).
//
// Nothing here launches anything: Take() only reports what was asked for, and
// main.cpp decides when to act on it - which is always at the moment a homebrew
// session ends, never while one is on screen.

namespace sl::sys::hb {

    struct Request {
        LaunchMode mode  = LaunchMode::Applet;
        u64        donor = 0;                    // 0 = use the configured donor
        char       nro[MaxNroPath]  = {};
        char       argv[MaxArgv]    = {};
    };

    class LaunchQueue {
        public:
            // Drop anything left over from a previous boot. A request queued
            // before a crash or a power cut has no business running now.
            static void Reset();

            // Take the oldest valid request, if there is one. The file is
            // deleted either way - a request the daemon cannot make sense of is
            // logged and discarded, never retried, so nothing can wedge the
            // console into relaunching the same bad .nro forever.
            static bool Take(Request &out);
    };

} // namespace sl::sys::hb
