// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXImportSession.m, BXAppController.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// The import of one game as a transaction, without Zune or AROS headers, so
// it is unit-tested on the host (src/ui/tests/import_controller_test.cpp).
//
// The controller owns the import's state, its copy and its installer flags,
// makes every stage change and is the only caller of
// ImportSession::discardGamebox: the UI asks the user and passes the
// answer on, but never decides what is deleted. Every way an unfinished
// import ends has its own outcome, and an import still unfinished when the
// controller goes (the end of BoxerUI, an early return, an exception)
// takes its own gamebox with it and nothing else (IS:1579 _cleanup).
#ifndef BOXER_UI_IMPORT_CONTROLLER_H
#define BOXER_UI_IMPORT_CONTROLLER_H

#include "session.h"

#include "../model/importsource.h"
#include "../model/sourcecopy.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace boxer {
class Gamebox;
}

namespace boxer_ui {

// How an unfinished import ended. None: it has not ended.
enum class ImportEnd {
    None,
    Cancelled,         // Back: the source was forgotten; nothing had been created
    WindowClosed,      // the window closed before anything was created
    Stopped,           // the user stopped an import that had made its gamebox
    CopyFailed,        // the source could not be copied (or the copy could not start)
    InstallerFailed,   // the installer's session failed
    Exited,            // the controller ended with the import unfinished
};
const char *importEndName(ImportEnd end);

// Why a call was refused.
enum class ImportRefusal {
    None,
    NotNow,      // not at this stage (the UI offers no such action then)
    Gamebox,     // the gamebox could not be created
    Launcher,    // the startup program could not be saved
};

// The result of one controller call.
// - end == None, error empty: done; the import goes on (stage() says where).
// - refusal set: refused; nothing changed (e.g. the gamebox could not be
//   created, the startup program could not be saved); error says why.
// - end != None: the import ended; it is back at WaitingForSource.
struct ImportOutcome {
    ImportEnd end = ImportEnd::None;
    ImportRefusal refusal = ImportRefusal::None;
    std::string error;          // why it was refused or why it ended (CopyFailed, InstallerFailed)
    std::string warning;        // done, with a part that failed (the icon of a finished gamebox)
    std::string gameName;       // of the import's gamebox, taken before it was removed
    std::string sourcePath;     // the source folder (never changed by the import)
    bool hadGamebox = false;    // the import had made its gamebox
    bool removed = true;        // that gamebox is gone (true when there was none)
    std::string cleanupError;   // why it could not be removed

    bool ended() const { return end != ImportEnd::None; }
    bool refused() const { return refusal != ImportRefusal::None; }
};

class ImportController {
public:
    using Report = std::function<void(const std::string &)>;
    // Records the cover and writes the sidecar icon of a gamebox that has
    // just got its startup program (main.cpp: ensureCoverRecord and
    // writeCoverIcon). False with a message; the import is finished anyway.
    using DecorateFinished = std::function<bool(boxer::Gamebox &box, const std::string &source, std::string &error)>;

    explicit ImportController(Report report = {}, DecorateFinished decorate = {});
    // finishAtExit(); never throws.
    ~ImportController();
    ImportController(const ImportController &) = delete;
    ImportController &operator=(const ImportController &) = delete;

    const boxer::ImportSession &session() const { return s_; }
    boxer::ImportStage stage() const { return s_.stage; }
    std::string gameName() const { return s_.gameName(); }

    // The import has made its gamebox and not finished it: closing or
    // stopping it now discards that gamebox, so the user is asked first.
    bool holdsUnfinishedGamebox() const;
    bool copying() const { return copying_; }
    bool installing() const { return s_.stage == boxer::ImportStage::RunningInstaller; }
    // An installer session is waiting to run or running.
    bool installerPending() const { return installerPending_; }

    // BXAppController orderFrontImportGamePanel. Started: a fresh import at
    // the dropzone (any finished or empty earlier import is forgotten).
    // Resumed: an import holding an unfinished gamebox goes on; the window
    // shows it. Busy: its installer is running; the window opens when it ends.
    enum class Open { Started, Resumed, Busy };
    Open open();

    // IS:importFromSourceURL and installerScanDidFinish:.
    boxer::SourceCheck chooseSource(const std::string &path, const boxer::DataLocations &locations,
                                    const std::string &gamesFolder, std::string *message = nullptr);
    boxer::InstallerScanError applyScan(const boxer::InstallerScanResult &result);
    void skipInstaller();
    // Back from the installer choice or the ready page: Cancelled. Refused
    // once the import has made its gamebox (the Back button is ghosted then).
    ImportOutcome back();

    // Creates the gamebox and marks the installer at `index` of the scan to
    // run; the UI then runs the installer's session. Refused: bad index or
    // no gamebox.
    ImportOutcome launchInstaller(size_t index, const std::string &gamesFolder);
    // The close question while the installer runs: Finish Importing
    // (stop = false) or Stop Importing (stop = true). The UI then asks the
    // core to quit.
    void endInstallerRequested(bool stop);
    bool installerStopRequested() const { return installerStop_; }
    // IS emulatorDidFinish: / finishInstaller. Goes on (ChoosingStartupProgram
    // when the installer left files in C.harddisk, else ReadyToFinalize) or
    // ends: Stopped by the user, InstallerFailed when the session failed.
    ImportOutcome installerEnded(SessionEnd end, const std::string &sessionError = "");

    // importSourceFiles (IS:745-1037): the gamebox (unless an installer made
    // it already), the destination and the list of files. Refused when the
    // gamebox cannot be created; CopyFailed when the copy cannot start.
    ImportOutcome startCopy(const std::string &gamesFolder);
    // Copies about `budget` bytes. Done: stage() is ChoosingStartupProgram.
    // CopyFailed: the copy's error, gamebox removed.
    ImportOutcome copyStep(uint64_t budget);
    // Progress of the running copy, 0..1000.
    long copyPermille() const;
    uint64_t copiedBytes() const { return copy_.copiedBytes(); }
    uint64_t totalBytes() const { return copy_.totalBytes(); }
    size_t fileCount() const { return copy_.fileCount(); }
    // Test hook: passed to every copy (SourceCopy::writeFault).
    std::function<bool(const std::string &, uint64_t)> copyWriteFault;

    // The user confirmed "Stop Importing" (while copying, or closing the
    // window of an import that holds its gamebox). Stopped; refused when
    // there is nothing to stop.
    ImportOutcome stop();
    // The window closed. WindowClosed when nothing had been created; refused
    // while the import holds an unfinished gamebox (stop() first) or runs
    // its installer. A finished import stays finished.
    ImportOutcome windowClosed();

    // BXImportFinishedPanelController: the startup program, relative to the
    // gamebox ("C.harddisk/..."). Refused when it cannot be saved; else
    // Finished, with `warning` when the icon could not be written.
    ImportOutcome useStartupProgram(const std::string &relativePath);
    // The finished gamebox was renamed by the UI (renameGameboxPair).
    void gameboxRenamed(const std::string &newPath);

    // Ends an unfinished import: closes the copy and removes the import's own
    // gamebox (and retries any whose removal failed earlier). Exited when
    // there was something to end, else None. Idempotent; never throws.
    ImportOutcome finishAtExit() noexcept;

private:
    ImportOutcome endImport(ImportEnd end, const std::string &why);
    void say(const std::string &line) noexcept;
    void say(const char *head, const char *tail) noexcept;
    boxer::ImportSession s_;
    boxer::SourceCopy copy_;
    bool copying_ = false;
    bool installerPending_ = false, installerStop_ = false;
    // Imports whose gamebox could not be removed; retried at exit.
    std::vector<boxer::ImportSession> unremoved_;
    Report report_;
    DecorateFinished decorate_;
};

}  // namespace boxer_ui
#endif
