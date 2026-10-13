// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXImportSession.m, BXAppController.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "import_controller.h"

#include "../model/gamebox.h"

#include <exception>
#include <utility>

namespace boxer_ui {

using boxer::ImportStage;

const char *importEndName(ImportEnd end) {
    switch (end) {
    case ImportEnd::None: return "not ended";
    case ImportEnd::Cancelled: return "cancelled";
    case ImportEnd::WindowClosed: return "window closed";
    case ImportEnd::Stopped: return "stopped";
    case ImportEnd::CopyFailed: return "copy failed";
    case ImportEnd::InstallerFailed: return "installer failed";
    case ImportEnd::Exited: return "unfinished at exit";
    }
    return "?";
}

static ImportOutcome refusedWith(const std::string &why, ImportRefusal kind = ImportRefusal::NotNow) {
    ImportOutcome o;
    o.refusal = kind;
    o.error = why;
    return o;
}

ImportController::ImportController(Report report, DecorateFinished decorate)
    : report_(std::move(report)), decorate_(std::move(decorate)) {}

ImportController::~ImportController() { finishAtExit(); }

void ImportController::say(const std::string &line) noexcept {
    try {
        if (report_) report_(line);
    } catch (...) {
        // The log is the only place to say it; cleanup must go on.
    }
}

// For the exception handlers: the line is built inside the guard.
void ImportController::say(const char *head, const char *tail) noexcept {
    try {
        say(std::string(head) + tail);
    } catch (...) {
        // As above: no memory left even for the line.
    }
}

bool ImportController::holdsUnfinishedGamebox() const {
    return !s_.createdGamebox.empty() && s_.stage != ImportStage::Finished;
}

ImportController::Open ImportController::open() {
    if (installerPending_ || installing()) return Open::Busy;
    if (copying_ || holdsUnfinishedGamebox()) return Open::Resumed;
    s_ = boxer::ImportSession();
    return Open::Started;
}

ImportController::Busy ImportController::busy() const {
    if (installerPending_ || installing()) return Busy::Installer;
    if (copying_) return Busy::Copying;
    if (holdsUnfinishedGamebox()) return Busy::UnfinishedGamebox;
    return Busy::None;
}

bool ImportController::acceptsSource(std::string *why) const {
    const std::string name = "\"" + s_.gameName() + "\"";
    std::string text;
    switch (busy()) {
    case Busy::None:
        if (why) why->clear();
        return true;
    case Busy::Installer:
        text = "The installer of " + name + " is running.\n\n"
               "Finish or stop that import before importing another game.";
        break;
    case Busy::Copying:
        text = "Boxer is still copying the files of " + name + " into its gamebox.\n\n"
               "Wait until the copy has finished, or stop it, before importing another game.";
        break;
    case Busy::UnfinishedGamebox:
        text = "The import of " + name + " has not finished: its gamebox has been made, "
               "but the import is not complete.\n\n"
               "Finish it, or stop it, before importing another game.";
        break;
    }
    if (why) *why = text;
    return false;
}

boxer::SourceCheck ImportController::chooseSource(const std::string &path, const boxer::DataLocations &locations,
                                                  const std::string &gamesFolder, std::string *message) {
    if (!acceptsSource(message)) return boxer::SourceCheck::Missing;
    if (!s_.createdGamebox.empty()) {
        if (message) *message = "This import has already started.";
        return boxer::SourceCheck::Missing;
    }
    // Checked before anything changes: a refused folder leaves the source
    // already chosen (and its scan) as they were.
    const boxer::SourceCheck c = boxer::checkImportSource(path, locations, gamesFolder, message);
    if (c != boxer::SourceCheck::Ok) return c;
    if (!s_.sourcePath.empty()) say("import: source replaced, earlier scan dropped: \"" + s_.sourcePath + "\"");
    s_.cancelSourceSelection();
    return s_.chooseSource(path, locations, gamesFolder, message);
}

bool ImportController::applyScan(const boxer::InstallerScanResult &result, const std::string &scannedPath,
                                 boxer::InstallerScanError *error) {
    if (s_.stage != ImportStage::LoadingSource || scannedPath != s_.sourcePath) {
        say("import: stale scan of \"" + scannedPath + "\" ignored");
        return false;
    }
    const boxer::InstallerScanError e = s_.applyScan(result);
    if (error) *error = e;
    return true;
}

void ImportController::skipInstaller() { s_.skipInstaller(); }

ImportOutcome ImportController::back() {
    if (!s_.createdGamebox.empty() || copying_ || installerPending_)
        return refusedWith("the import has made its gamebox; stop it instead");
    ImportOutcome o;
    o.end = ImportEnd::Cancelled;
    o.sourcePath = s_.sourcePath;
    s_.cancelSourceSelection();
    say("import: back to source selection");
    return o;
}

ImportOutcome ImportController::launchInstaller(size_t index, const std::string &gamesFolder) {
    if (s_.stage != ImportStage::WaitingForInstaller || index >= s_.scan.installers.size())
        return refusedWith("no installer chosen");
    std::string err;
    if (!s_.createGamebox(gamesFolder, &err)) {
        say("import: gamebox not created: " + err);
        return refusedWith(err, ImportRefusal::Gamebox);
    }
    s_.installerPath = s_.scan.installers[index];
    s_.stage = ImportStage::RunningInstaller;
    installerPending_ = true;
    installerStop_ = false;
    say("import: created \"" + s_.createdGamebox + "\" (id " + s_.createdIdentifier + "), running installer " +
        s_.installerPath);
    return {};
}

void ImportController::endInstallerRequested(bool stop) {
    if (installing()) installerStop_ = stop;
}

ImportOutcome ImportController::installerEnded(SessionEnd end, const std::string &sessionError) {
    if (!installerPending_ || !installing()) return refusedWith("no installer was running");
    installerPending_ = false;
    const bool stop = installerStop_;
    installerStop_ = false;
    if (!importContinuesAfterInstaller(end, stop)) {
        if (sessionEndIsError(end)) {
            say("import: installer session failed; the import stops");
            return endImport(ImportEnd::InstallerFailed, sessionError.empty() ? sessionEndName(end) : sessionError);
        }
        return endImport(ImportEnd::Stopped, "stopped");
    }
    if (s_.gameDidInstall()) {
        s_.stage = ImportStage::ChoosingStartupProgram;
        say("import: installer finished, files in the gamebox");
    } else {
        // Nothing was installed: the source files are imported instead (IS:745).
        s_.stage = ImportStage::ReadyToFinalize;
        say("import: installer finished without installing files");
    }
    return {};
}

ImportOutcome ImportController::startCopy(const std::string &gamesFolder) {
    if (s_.stage != ImportStage::ReadyToFinalize || copying_) return refusedWith("the import is not ready to copy");
    std::string err, dest;
    if (s_.createdGamebox.empty() && !s_.createGamebox(gamesFolder, &err)) {
        say("import: gamebox not created: " + err);
        return refusedWith(err, ImportRefusal::Gamebox);
    }
    copy_.writeFault = copyWriteFault;
    if (!s_.prepareCopyDestination(dest, &err) || !copy_.prepare(s_.sourcePath, dest, &err))
        return endImport(ImportEnd::CopyFailed, err);
    copying_ = true;
    s_.stage = ImportStage::ImportingSourceFiles;
    say("import: copying " + std::to_string(copy_.fileCount()) + " files, " + std::to_string(copy_.totalBytes()) +
        " bytes from \"" + s_.sourcePath + "\" to \"" + dest + "\"");
    return {};
}

ImportOutcome ImportController::copyStep(uint64_t budget) {
    if (!copying_) return {};
    std::string err;
    if (!copy_.step(budget, &err)) return endImport(ImportEnd::CopyFailed, err);
    if (copy_.finished()) {
        say("import: copied " + std::to_string(copy_.copiedBytes()) + " bytes");
        copy_.close();
        copying_ = false;
        s_.stage = ImportStage::ChoosingStartupProgram;
    }
    return {};
}

long ImportController::copyPermille() const {
    const uint64_t total = copy_.totalBytes();
    return total ? (long)(copy_.copiedBytes() * 1000 / total) : 1000;
}

ImportOutcome ImportController::stop() {
    // While the installer runs its session uses the gamebox: the stop is
    // asked of the session (endInstallerRequested) and done when it ended.
    if (installing()) return refusedWith("the installer is running");
    if (!copying_ && !holdsUnfinishedGamebox()) return refusedWith("nothing to stop");
    return endImport(ImportEnd::Stopped, "stopped");
}

ImportOutcome ImportController::windowClosed() {
    if (installing()) return refusedWith("the installer is running");
    if (s_.stage == ImportStage::Finished) return {};
    if (copying_ || holdsUnfinishedGamebox()) return refusedWith("the import holds an unfinished gamebox");
    ImportOutcome o;
    o.end = ImportEnd::WindowClosed;
    o.sourcePath = s_.sourcePath;
    // Nothing has been created at these stages, so closing only forgets
    // the source (IS:311 asks first only once a gamebox exists).
    s_ = boxer::ImportSession();
    say("import: window closed, nothing created");
    return o;
}

ImportOutcome ImportController::useStartupProgram(const std::string &relativePath) {
    if (s_.stage != ImportStage::ChoosingStartupProgram) return refusedWith("no startup program is being chosen");
    boxer::Gamebox box;
    std::string err;
    const bool ok = box.open(s_.createdGamebox, &err) && boxer::setDefaultLauncher(box, relativePath, &err);
    say("import: startup program " + relativePath + " -> " + (ok ? "saved" : "NOT saved: " + err));
    if (!ok) return refusedWith(err, ImportRefusal::Launcher);
    ImportOutcome o;
    // The sidecar icon shows the cover. A failure is reported but does not
    // undo the import: the gamebox is complete without it.
    std::string iconErr;
    if (decorate_ && !decorate_(box, s_.sourcePath, iconErr))
        o.warning = iconErr.empty() ? std::string("the icon could not be written") : iconErr;
    s_.stage = ImportStage::Finished;
    return o;
}

void ImportController::gameboxRenamed(const std::string &newPath) {
    if (s_.stage == ImportStage::Finished && !newPath.empty()) s_.createdGamebox = newPath;
}

// The one place an unfinished import ends: its own gamebox goes (the model
// refuses anything that is not still that gamebox), the source stays.
ImportOutcome ImportController::endImport(ImportEnd end, const std::string &why) {
    ImportOutcome o;
    o.end = end;
    o.error = why;
    o.gameName = s_.gameName();
    o.sourcePath = s_.sourcePath;
    o.hadGamebox = !s_.createdGamebox.empty();
    copy_.close();
    copying_ = false;
    installerPending_ = installerStop_ = false;
    std::string err;
    o.removed = s_.discardGamebox(&err);
    if (!o.removed) {
        o.cleanupError = err;
        // Kept for one more try at exit, as before; a new import may start.
        unremoved_.push_back(s_);
    }
    say(std::string("import: ") + importEndName(end) + " (" + why + "); unfinished gamebox " +
        (o.hadGamebox ? (o.removed ? "deleted" : "NOT deleted: " + err) : "none"));
    s_ = boxer::ImportSession();
    return o;
}

ImportOutcome ImportController::finishAtExit() noexcept {
    ImportOutcome o;
    try {
        copy_.close();
        copying_ = false;
        installerPending_ = installerStop_ = false;
        if (holdsUnfinishedGamebox()) {
            o.end = ImportEnd::Exited;
            o.gameName = s_.gameName();
            o.sourcePath = s_.sourcePath;
            o.hadGamebox = true;
            std::string err;
            o.removed = s_.discardGamebox(&err);
            o.cleanupError = err;
            say("import: unfinished at exit; gamebox " + (o.removed ? std::string("deleted") : "NOT deleted: " + err));
            s_ = boxer::ImportSession();
        }
        std::vector<boxer::ImportSession> again;
        again.swap(unremoved_);
        for (auto &u : again) {
            const std::string path = u.createdGamebox;
            std::string err;
            const bool ok = u.discardGamebox(&err);
            say("import: unfinished gamebox \"" + path + "\" at exit " + (ok ? "deleted" : "NOT deleted: " + err));
            if (o.end == ImportEnd::None) o.end = ImportEnd::Exited;
            if (!ok) o.removed = false;
        }
    } catch (const std::exception &e) {
        o.end = ImportEnd::Exited;
        o.removed = false;
        say("import: cleanup at exit failed: ", e.what());
    } catch (...) {
        o.end = ImportEnd::Exited;
        o.removed = false;
        say("import: cleanup at exit failed with an unknown exception", "");
    }
    return o;
}

}  // namespace boxer_ui
