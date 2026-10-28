# hbloader (sLaunch fork)

Fork of [nx-hbloader](https://github.com/switchbrew/nx-hbloader) (ISC license,
see `LICENSE.md`) used by sSystem to run homebrew. The built NSO is copied to
`assets/hbloader/main` and served via ECS in two flavors (same binary,
different npdm):

- `SdOut/slaunch/bin/hbloader` -- album applet slot (applet mode / Homebrew menu)
- `SdOut/slaunch/bin/hbloader_app` -- donor game slot (full-RAM title mode)

## Changes vs upstream (`source/main.c`)

1. **One-shot target file.** On first load, if `sdmc:/slaunch/hbtarget.txt`
   exists, the NRO path inside is loaded instead of `sdmc:/hbmenu.nro`
   ("target mode"). sSystem writes this file right before launching the
   loader, for both applet-mode and title-mode homebrew.
2. **Exit instead of looping.** Stock hbloader reloads the default NRO forever
   when the running NRO exits without chainloading. In target mode this
   relaunched the just-closed homebrew endlessly. The fork terminates the
   process instead, so sSystem sees the applet/application finish and brings
   sMenu back.
3. **Missing target is not fatal.** In target mode a missing/unreadable NRO
   exits back to the menu instead of hard-aborting with a crash dialog.
4. **argv on the second line.** `hbtarget.txt` may carry a second line holding
   the argv to hand the NRO. Without it the loader falls back to the quoted
   path, which is what hbmenu passes and what this fork did before. sSystem
   writes the line whenever the menu (or a queued request) supplied arguments,
   so they survive the launch instead of being dropped at the door.
5. **Chainloading to application mode (opt-in).** An .nro running in the applet
   slot gets a small heap, and nothing inside this process can promote itself to
   a full-RAM application - that needs sSystem to serve this loader into a donor
   game's slot. So when `sdmc:/slaunch/config/hb_chain_app.txt` holds a `1` and
   the running NRO chainloads, the loader writes the target to sSystem's drop
   box (`sdmc:/slaunch/hb_queue/`, format in `sCommon/sl/sys/HbLaunchRequest.hpp`)
   and exits instead of loading it here; sSystem then starts it in the donor
   slot. Only from an applet - in a donor slot the process already has the RAM,
   so loading in place is strictly better - and only if the request reaches the
   card, so a failed write costs nothing. Without the opt-in file, chainloading
   behaves exactly as it always did.

Behavior with no target file (Homebrew menu case) is unchanged from upstream:
homebrew exiting returns to hbmenu.

Building (`make hbloader` from the repo root) produces `hbl.nso` and copies it
to `assets/hbloader/main`. The npdms in `assets/hbloader/` are prebuilt
(applet: `main.npdm`, application: `main_app.npdm` from `hbl_app.json`) and do
not need rebuilding for code-only changes.
