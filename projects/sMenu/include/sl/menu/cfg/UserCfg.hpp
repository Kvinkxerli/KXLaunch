#pragma once
#include <switch.h>
#include <string>

// Per-account configuration paths.
//
// Everything the user chooses - theme, font, layout, favourites, order, tile
// sizes, widget positions, music, pinned homebrew - lives under
//
//     sdmc:/slaunch/config/users/<account id>/
//
// so two people sharing a console do not share a menu. What stays in
// sdmc:/slaunch/config is what belongs to the console rather than to a person:
// the donor title, the SteamGridDB key, the applet-slot override, the
// anti-alias safety marker and the daemon's IPC drop files. Shared *content* -
// themes/, icon_packs/, fonts/, music/, covers/, widgets/, lang/ - stays where
// it is too; only the choice of which one to use is per user.
//
// The account id is the 128-bit id libnx reports, printed as 32 hex digits.
// It is not meant to be readable, so EnsureDir() also drops a name.txt holding
// the account's nickname - that is how you tell the folders apart on a PC.
//
// Installs from before this existed keep their settings: the first account to
// run the menu after the update inherits the old sdmc:/slaunch/config files
// (see MigrateLegacy). The originals are left untouched, so downgrading loses
// nothing; a marker file stops a second account from inheriting them as well.

namespace sl::menu::cfg {

    // Point every path at this account. Call once, as early as the uid is
    // known and before anything reads a config file. An invalid uid (no
    // accounts, or a launcher that did not hand one over) selects a shared
    // "default" folder rather than falling back to the console-wide files, so
    // there is exactly one place a given setting can be.
    void SetUser(AccountUid uid);

    AccountUid User();

    // "sdmc:/slaunch/config/users/<id>"
    std::string Dir();

    // Dir() + "/" + filename. Cheap; call it at the point of use.
    std::string Path(const char *filename);

    // Create the account's folder (and everything above it). Savers call this
    // before writing; readers do not need it.
    void EnsureDir();

    // Same, for a folder inside the account's - "widgets", say.
    void EnsureSubdir(const char *sub);

    // Record the account's nickname next to its settings. Written only when it
    // is missing or has changed, so this costs one read per boot.
    void NoteNickname(const char *nick);

    // One-shot: seed this account from the pre-per-user config files. Does
    // nothing once any account has been seeded. Safe to call every boot.
    void MigrateLegacy();

} // namespace sl::menu::cfg
