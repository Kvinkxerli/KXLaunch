#pragma once
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// Homebrew launch requests: how a running .nro asks sLaunch to start another.
//
// A homebrew that wants to hand over to another homebrew normally calls
// envSetNextLoad() and exits - hbloader loads the next .nro itself, in the same
// process, and that keeps working untouched. What it cannot do is change the
// terms it runs under: an .nro started from the Homebrew menu lives in an applet
// slot with a small heap, and nothing inside that process can promote itself to
// a full-RAM application. Only the daemon can, by serving hbloader into a donor
// game's slot.
//
// So this is a drop box the daemon watches:
//
//     sdmc:/slaunch/hb_queue/<anything>.req
//
// one request per file, written as key=value lines:
//
//     mode=app                          applet (default) or app
//     nro=sdmc:/switch/sphaira.nro      required; must exist and end in .nro
//     argv="sdmc:/switch/sphaira.nro"   optional; hbloader's default is the
//                                       quoted path, same as hbmenu passes
//     donor=0100000000010000            optional, app mode only; 0 or absent
//                                       means "whatever donor is configured"
//
// Write it with Write() below, which lands the file atomically (temp + rename)
// so the daemon can never read a half-written request. Requests are one-shot:
// the daemon deletes each file as it takes it, valid or not, so a bad request
// costs one launch rather than a boot loop. They are only acted on when the
// homebrew that queued one has exited - the daemon is busy running it until
// then - and a queued request chains straight into the next launch instead of
// bouncing through the menu.
//
// This is a file drop rather than an IPC service on purpose: any homebrew can
// write a file with plain stdio, with no sysmodule client library, no service
// handle, and nothing to keep in sync across versions.

namespace sl::sys::hb {

    constexpr const char *QueueDir    = "sdmc:/slaunch/hb_queue";
    constexpr const char *QueueSuffix = ".req";
    constexpr const char *QueueTemp   = "sdmc:/slaunch/hb_queue/pending.tmp";

    // Path/argv limits. The daemon rejects anything longer rather than cutting
    // it short - a truncated path is a different file, and launching a
    // different file than the one asked for is worse than not launching.
    constexpr size_t MaxNroPath = 512;
    constexpr size_t MaxArgv    = 512;

    enum class LaunchMode {
        Applet = 0,   // library-applet slot: small heap, quick, returns to sLaunch
        App    = 1,   // donor game's slot: full RAM and application permissions
    };

    // Queue one request, replacing any this helper queued earlier: handing over
    // is a single "run this next", not a playlist, and a homebrew that changes
    // its mind must not leave the first choice behind to run afterwards.
    // Returns false only if it could not be written; whether the request is
    // acceptable is the daemon's decision, made when it takes it.
    inline bool Write(const char *nro_path,
                      const char *argv = nullptr,
                      LaunchMode  mode = LaunchMode::Applet,
                      unsigned long long donor_id = 0) {
        if (!nro_path || !nro_path[0] || strlen(nro_path) >= MaxNroPath) return false;
        if (argv && strlen(argv) >= MaxArgv) return false;

        mkdir("sdmc:/slaunch", 0777);
        mkdir(QueueDir, 0777);

        FILE *fp = fopen(QueueTemp, "w");
        if (!fp) return false;
        fprintf(fp, "mode=%s\n", mode == LaunchMode::App ? "app" : "applet");
        fprintf(fp, "nro=%s\n", nro_path);
        if (argv && argv[0]) fprintf(fp, "argv=%s\n", argv);
        if (mode == LaunchMode::App && donor_id) fprintf(fp, "donor=%016llX\n", donor_id);
        const bool ok = (fflush(fp) == 0);
        fclose(fp);
        if (!ok) { remove(QueueTemp); return false; }

        // Rename last: until this succeeds there is no .req for the daemon to
        // find, so it never sees a partial file.
        char dst[128];
        snprintf(dst, sizeof(dst), "%s/request%s", QueueDir, QueueSuffix);
        remove(dst);
        if (rename(QueueTemp, dst) != 0) { remove(QueueTemp); return false; }
        return true;
    }

} // namespace sl::sys::hb
