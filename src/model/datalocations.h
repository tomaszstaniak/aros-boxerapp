// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXBaseAppController+BXSupportFiles.m, BXSession+BXFileManagement.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Where Boxer keeps its own data on AROS (see
// documentation/development.md), in place of the original's
// ~/Library/Preferences and ~/Library/Application Support/Boxer
// (BXBaseAppController+BXSupportFiles.m gameStatesURLForGamebox:,
// BXSession+BXFileManagement.m currentGameStateURL / shadowURLForDrive:).
//
//   prefs:     ENVARC:Boxer/Boxer.prefs (persistent), ENV:Boxer/Boxer.prefs
//              (the live copy AROS makes of ENVARC: at boot)
//   data dir:  user-chosen; proposed default "<games folder>/Boxer Data"
//     Gamebox States/<identifier>/Current.boxerstate/<drive file name>/
//     Screenshots/
//
// The data directory is chosen ONCE and kept in the user prefs.
// It is never derived from where an opened gamebox
// lives: a gamebox's state is found by its identifier under that one
// directory, so moving or renaming the gamebox keeps its state. The games
// folder is not assumed writable; prepareDataDir() checks and reports.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace boxer {

class Gamebox;
struct BundledDrive;

struct DataLocations {
	std::string envPrefsPath = "ENV:Boxer/Boxer.prefs";
	std::string envarcPrefsPath = "ENVARC:Boxer/Boxer.prefs";
	std::string dataDir;

	static std::string defaultDataDir(const std::string &gamesFolder);

	std::string gameStatesDir() const;                          // <data>/Gamebox States
	std::string gameStateDir(const std::string &identifier) const; // .../<id>
	std::string currentStatePath(const std::string &identifier) const; // .../<id>/Current.boxerstate
	std::string shadowRoot(const std::string &identifier, const BundledDrive &drive) const;
	std::string screenshotsDir() const;
};

enum class DataDirStatus {
	Ready,        // exists (or was created) and a probe file could be written
	NotWritable,  // exists but the probe failed
	CannotCreate, // missing and could not be created
	Missing,      // missing and mayCreate was false (a configured directory vanished)
	Unset,
};

// Creates the data directory if missing, then probes it. Never falls back to
// another location silently: on anything but Ready the caller must ask the
// user for a different directory.
DataDirStatus prepareDataDir(const std::string &dataDir, std::string *message = nullptr,
                             bool mayCreate = true);

// Boxer Data is excluded from game scanning and import: true for the
// data directory itself, anything below it, and for any directory named
// "Boxer Data" (also when the configured data dir is elsewhere, so a stale
// default under the games folder is not offered as games either).
bool isExcludedFromScan(const std::string &path, const DataLocations &locations);

// ---- user prefs (ENVARC:Boxer/Boxer.prefs) ----
// Plain text, one "Key=value" per line; '#' starts a comment line. Unknown
// keys are kept and written back, so an older BoxerUI does not drop what a
// newer one stored.
struct UserPrefs {
	std::string dataDir;                        // key DataDir
	std::string gamesFolder;                    // key GamesFolder
	std::map<std::string, std::string> other;   // every other key, verbatim
};
// False (with the line number in error) on a line that is not blank, not a
// comment and has no '='. The prefs read so far stay in out.
bool parsePrefs(const std::string &text, UserPrefs &out, std::string *error = nullptr);
std::string serializePrefs(const UserPrefs &prefs);

enum class PrefsSource { Env, Envarc, None, Unreadable };
// ENV: is the live copy AROS makes of ENVARC: at boot; it is read first, and
// ENVARC: when ENV: has no copy (e.g. ENV: cleared). recoverReplace() runs on
// both before reading, so an interrupted save is finished or rolled back.
// Unreadable also when no prefs file exists but an interrupted save left
// files next to one (fsutil::describeLeftovers): they are not used, and the
// error names them for the user.
PrefsSource loadUserPrefs(const DataLocations &where, UserPrefs &out, std::string *error = nullptr);
// ENVARC: (persistent) first, then ENV:, each through fsutil::replaceFile so
// a failed write keeps the last good version. An empty envPrefsPath skips
// ENV:. False if ENVARC: could not be written; envError reports an ENV:
// failure separately (the choice still survives a reboot then).
bool saveUserPrefs(const DataLocations &where, const UserPrefs &prefs, std::string *error = nullptr,
                   std::string *envError = nullptr);

// Which data directory a session uses, decided before anything is created.
enum class DataDirOrigin {
	Override,   // DATA argument: this session only, prefs unchanged
	Chosen,     // DATADIR argument or BOXER_DATADIR variable: becomes the configured one
	Configured, // DataDir from the prefs
	Proposal,   // first run: "<games folder>/Boxer Data", must be confirmed and saved
	None,       // nothing known (no gamebox to propose a games folder from)
};
struct DataDirChoice {
	std::string dataDir;
	DataDirOrigin origin = DataDirOrigin::None;
	bool save = false;          // store into the prefs once prepareDataDir() is Ready
	bool mayCreate = true;      // false for Configured: a vanished configured
	                            // directory is reported, never silently recreated empty
};
// gamesFolder is only used for the first-run proposal; the drawer holding
// the gamebox being opened is the natural one then. It plays no part once a
// directory is configured.
DataDirChoice chooseDataDir(const std::string &sessionOverride, const std::string &chosen,
                            const UserPrefs &prefs, const std::string &gamesFolder);
const char *dataDirOriginName(DataDirOrigin o);

// ---- games folder (GF:72-104 defaultGamesFolderURL) ----
// The original proposes ~/DOS Games. AROS has no home directory: Work: when
// that volume or assign exists, else SYS:. Existence says nothing about
// write access; the caller checks with prepareDataDir() and asks.
std::string defaultGamesFolder(bool haveWork);
// GF:398 _isReservedURL: a games folder may not be a gamebox or lie inside
// one, nor lie inside the data directory or any "Boxer Data" (its contents
// would be scanned as games). The data directory below the games folder is
// the normal default and is fine. False with a reason when refused.
bool acceptableGamesFolder(const std::string &path, const DataLocations &locations,
                           std::string *why = nullptr);

// GF:778 revealGamesFolder and GF:732 promptForMissingGamesFolderInWindow:
// Browse opens the games folder when it is a directory. A configured folder
// that is gone (deleted, renamed, its disk not mounted, or a file in its
// place) is Missing, and the user is asked to locate it; nothing is
// recreated silently. NotSet when no games folder has been chosen yet.
enum class GamesFolderBrowse { Open, Missing, NotSet };
GamesFolderBrowse browseGamesFolder(const std::string &gamesFolder);

// A configured data directory that is gone most often went along with the
// games folder it lived in (the default is "<games folder>/Boxer Data").
// findMovedDataDir() looks for it where it would be now, in each folder of
// gamesFolders (the current games folder, the drawer of the gamebox being
// opened): at the same place relative to oldGamesFolder when the old data
// directory was inside it, under its old name, and under the default name.
//
// A name alone is never taken as proof. A candidate counts as the moved data
// directory only when its "Gamebox States" holds the state of a gamebox in
// that games folder (the state drawers are named by gamebox identifier).
//   Found      exactly one candidate holds state of the folder's games
//   Ambiguous  more than one does
//   Uncertain  a candidate exists but holds no state, or the folder has no
//              gamebox to compare with: only the user can tell
//   NotFound   no candidate, or only ones holding other games' state
// Nothing is created, moved or written.
enum class MovedDataDir { Found, Ambiguous, Uncertain, NotFound };
struct MovedDataDirSearch {
	MovedDataDir result = MovedDataDir::NotFound;
	std::string path;                   // Found: the directory; Uncertain: where to start looking
	std::vector<std::string> matching;  // Found/Ambiguous: candidates holding state of the folder's games
	size_t matchedGames = 0;            // Found: games of the folder whose state is there
};
MovedDataDirSearch findMovedDataDir(const std::string &missingDataDir, const std::string &oldGamesFolder,
                                    const std::vector<std::string> &gamesFolders);
std::vector<std::string> movedDataDirCandidates(const std::string &missingDataDir,
                                                const std::string &oldGamesFolder,
                                                const std::vector<std::string> &gamesFolders);
// Identifiers of the gameboxes in a games folder and its drawers (three
// levels; gamebox contents and any "Boxer Data" are not searched), as
// state drawer names (safeFolderName). Gameboxes without one are skipped.
std::vector<std::string> gameboxStateNamesIn(const std::string &gamesFolder);

// Characters that cannot appear in an AROS file name are replaced, so an
// identifier (UUID, hex digest or reverse-DNS) is always a usable folder.
std::string safeFolderName(const std::string &identifier);

} // namespace boxer
