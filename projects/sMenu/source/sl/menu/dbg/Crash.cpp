// Crash containment. A data abort in a library applet is a fatal error screen
// and a reboot; caught here instead, the menu writes down where it died and
// exits, and the daemon brings it straight back (see main.cpp for the safe
// mode a run of crashes turns on).
//
// Runs on its own stack with the heap possibly corrupt, so nothing here
// allocates: a fresh fs session, snprintf into stack buffers, raw writes.
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <sl/menu/dbg/Crash.hpp>

extern "C" {
    alignas(16) u8 __nx_exception_stack[0x8000];
    u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

namespace {

    void Append(FsFileSystem &sd, const char *path, const char *data, size_t len, bool truncate) {
        char p[FS_MAX_PATH] = {};
        strncpy(p, path, sizeof(p) - 1);
        if (truncate) fsFsDeleteFile(&sd, p);
        fsFsCreateFile(&sd, p, 0, 0);
        FsFile f;
        if (R_FAILED(fsFsOpenFile(&sd, p, FsOpenMode_Write | FsOpenMode_Append, &f))) return;
        s64 off = 0;
        fsFileGetSize(&f, &off);
        fsFileWrite(&f, off, data, len, FsWriteOption_Flush);
        fsFileClose(&f);
    }

    int ReadCount(FsFileSystem &sd) {
        char p[FS_MAX_PATH] = {};
        strncpy(p, sl::menu::dbg::kCrashCountPath + 5, sizeof(p) - 1);   // drop "sdmc:"
        FsFile f;
        if (R_FAILED(fsFsOpenFile(&sd, p, FsOpenMode_Read, &f))) return 0;
        char b[16] = {};
        u64 got = 0;
        fsFileRead(&f, 0, b, sizeof(b) - 1, FsReadOption_None, &got);
        fsFileClose(&f);
        int n = 0;
        for (u64 i = 0; i < got && b[i] >= '0' && b[i] <= '9'; i++) n = n * 10 + (b[i] - '0');
        return n;
    }

    // Base of the module holding `addr` (its .text mapping), so the log gives
    // offsets that line up with sMenu.elf whatever ASLR picked.
    u64 ModuleBase(u64 addr) {
        MemoryInfo mi = {};
        u32 pi = 0;
        if (R_FAILED(svcQueryMemory(&mi, &pi, addr))) return 0;
        return mi.addr;
    }

    bool Readable(u64 addr) {
        MemoryInfo mi = {};
        u32 pi = 0;
        if (addr & 7 || R_FAILED(svcQueryMemory(&mi, &pi, addr))) return false;
        return (mi.perm & Perm_R) && addr + 16 <= mi.addr + mi.size;
    }

} // namespace

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    const u64 base = ModuleBase((u64)(uintptr_t)&__libnx_exception_handler);
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
                     "--- sMenu crash (%s) desc=0x%x\n"
                     "pc  = sMenu+0x%lx\nlr  = sMenu+0x%lx\nsp  = 0x%lx\nfar = 0x%lx esr=0x%x\n",
                     SL_VERSION, ctx->error_desc,
                     ctx->pc.x - base, ctx->lr.x - base, ctx->sp.x, ctx->far.x, ctx->esr);
    // Frame-pointer walk; every frame is checked readable first, since a
    // corrupt chain must not fault again in here.
    u64 fp = ctx->fp.x;
    for (int i = 0; i < 24 && n < (int)sizeof(buf) - 64 && Readable(fp); i++) {
        const u64 *frame = (const u64 *)(uintptr_t)fp;
        const u64 ret = frame[1];
        if (!ret) break;
        n += snprintf(buf + n, sizeof(buf) - n, "bt%02d = sMenu+0x%lx\n", i, ret - base);
        if (frame[0] <= fp) break;   // the chain only ever goes up the stack
        fp = frame[0];
    }

    FsFileSystem sd;
    if (R_SUCCEEDED(fsOpenSdCardFileSystem(&sd))) {
        Append(sd, "/slaunch/crash.log", buf, (size_t)n, false);
        char c[16];
        const int cn = snprintf(c, sizeof(c), "%d\n", ReadCount(sd) + 1);
        Append(sd, sl::menu::dbg::kCrashCountPath + 5, c, (size_t)cn, true);
        fsFsClose(&sd);
    }
    // Exit instead of returning to the faulting instruction: the daemon sees
    // the menu close without a request and relaunches it.
    svcExitProcess();
}
