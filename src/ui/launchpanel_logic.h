// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/DOS window/BXLaunchPanelController.m,
// Boxer/BXSession.m, BXSession+BXFileManagement.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Launch panel logic without Zune: the rows the panel shows (favorites,
// recent programs, all programs per drive), the search filter and the
// session rules that decide when the panel appears. Host-testable
// (src/ui/tests/launchpanel_test.cpp).
#pragma once

#include <string>
#include <vector>

namespace boxer_ui {

enum class LPIcon { None, Favorites, Recent, AllPrograms, HardDisk, CDROM, Floppy };

struct LPRow {
    enum Kind { SectionHeading, DriveHeading, Favorite, Program } kind = Program;
    std::string title;
    std::string dosPath;      // "C:\GAME\PLAY.EXE"; empty when unresolved
    std::string arguments;
    std::string hostPath;     // the program, or the drive's source for a drive heading
    char drive = 0;
    LPIcon icon = LPIcon::None;
    int launcher = -1;        // index into the gamebox's launchers (Favorite)
    int recent = -1;          // index into the session's recent programs
    bool isHeading() const { return kind == SectionHeading || kind == DriveHeading; }
    // LauncherItem/LauncherFavorite second line: "%{value1}@ %{value2}@" of
    // dosPath and arguments.
    std::string subtitle() const;
};

// BXDOSFilenameTransformer: last path component, upper-cased.
std::string dosFilename(const std::string &path);

// _listItemForProgramAtURL:onDrive:withArguments:title: (title nil):
// file name, then the arguments if any.
LPRow programRow(const std::string &hostPath, const std::string &dosPath, char drive,
                 const std::string &arguments);
// _listItemForLauncher: title, or the file name when the title is empty.
LPRow favoriteRow(const std::string &title, const std::string &hostPath, const std::string &dosPath,
                  char drive, const std::string &arguments, int launcherIndex);
// _listItemForDrive: "Drive %1$@ (%2$@)" with letter and title, icon by type
// (0 hard disk, 1 CD-ROM, 2 floppy).
LPRow driveRow(char letter, const std::string &driveTitle, int driveType, const std::string &source);
LPRow sectionHeading(LPIcon which);   // Favorites / Recent / All Programs

// _syncRecentProgramRows input: one recent program with what the session
// knows about it.
struct LPRecent {
    std::string hostPath, dosPath, arguments;
    char drive = 0;
    bool accessible = false;   // logicalURLIsAccessibleInDOS:
};
struct LPLauncherRef { std::string hostPath, arguments; };
// BXLaunchPanelMaxRecentRows
constexpr int kMaxRecentRows = 3;
// At most three rows; programs not reachable in DOS and programs that match
// a launcher (same path, same arguments) are skipped.
std::vector<LPRow> recentRows(const std::vector<LPRecent> &recents,
                              const std::vector<LPLauncherRef> &launchers);

// _syncFilterKeywords: the field's text split at every whitespace character,
// duplicates dropped. Empty pieces are kept, as componentsSeparatedBy-
// CharactersInSet: returns them; an empty keyword matches nothing.
std::vector<std::string> filterKeywords(const std::string &text);

// _relevanceOfRow:forKeywords:
int relevance(const LPRow &row, const std::vector<std::string> &keywords);
// _rowsMatchingKeywords:inRows: headings skipped, relevance descending,
// ties by DOS path (case-insensitive).
std::vector<LPRow> rowsMatching(const std::vector<std::string> &keywords, const std::vector<LPRow> &rows);

// _syncDisplayedRows
std::vector<LPRow> displayedRows(const std::vector<LPRow> &favorites, const std::vector<LPRow> &recents,
                                 const std::vector<LPRow> &allPrograms,
                                 const std::vector<std::string> &keywords);

// --- session rules (BXSession.m) ---

// readFromURL: start on the launch panel instead of a startup program when
// the panel is allowed and alwaysShowLaunchPanel or the one-shot
// showLaunchPanel flag is set.
bool startWithLaunchPanel(bool allowsLauncherPanel, bool alwaysShow, bool showOnce);

enum class Completion { DoNothing, ShowLauncher, ShowPrompt };
// BXSuccessfulProgramRunningTimeThreshold
constexpr double kSuccessfulRunSeconds = 3.0;
// _behaviorAfterReturningToShellFromProcess: for a non-standalone session.
// loadingPanel: the window is still on its loading panel.
Completion afterReturnToShell(Completion requested, bool allowsLauncherPanel, bool internalProcess,
                              double runSeconds, bool loadingPanel);

}  // namespace boxer_ui
