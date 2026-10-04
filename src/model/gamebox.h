// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXGamebox.m, BXDrive.m, BXFileTypes.m, BXSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Gamebox model: a ".boxer" (or ".dosbox") directory and what the original
// read from it. Follows Boxer/BXGamebox.m, Boxer/BXDrive.m (name parsing),
// Boxer/BXFileTypes.m (type sets) and BXSession.m
// (configurationURLsForEmulator:, _mountDrivesForSession).
#pragma once

#include "plist.h"

#include <string>
#include <vector>

namespace boxer {

struct ConfigFile; // src/emulator/emulator.h

enum class DriveType { HardDisk, CDROM, Floppy };

// Type sets of BXFileTypes.m, decided by extension as AROS has no UTIs.
namespace drivetypes {
bool isGameboxName(const std::string &name);        // .boxer .dosbox
bool isMountableFolderName(const std::string &name); // .harddisk .harddrive .cdrom .floppy
bool isMountableImageName(const std::string &name);  // .cdmedia .iso .cdr .cue .inst .ima .vfd .img
// hddVolumeTypes / cdVolumeTypes / floppyVolumeTypes; false if none.
bool volumeType(const std::string &name, DriveType &type);
}

// +[BXDrive preferredDriveLetterForContentsOfURL:]: letter A-X (upper-cased)
// from "<L>" or "<L> <anything>" before the extension, only for mountable
// folders and images; 0 otherwise.
char preferredDriveLetter(const std::string &name);
// +[BXDrive preferredVolumeLabelForContentsOfURL:]: strip a known extension,
// a trailing " (N)" import suffix, then a leading "<L> " prefix.
std::string preferredVolumeLabel(const std::string &name);

struct BundledDrive {
	std::string sourcePath;  // the folder or image inside the gamebox
	std::string mountPath;   // what MOUNT/IMGMOUNT gets (tracks.cue for .cdmedia)
	char letter = 0;
	std::string label;
	DriveType type = DriveType::HardDisk;
	bool isImage = false;
	bool isGameboxRoot = false; // legacy gamebox: root mounted as C
	// Name of this drive's folder inside Current.boxerstate
	// (BXSession shadowURLForDrive:): the source file name, or "C.harddisk"
	// for a legacy root drive.
	std::string shadowName;
	bool queued = false; // letter already taken by an earlier drive (BXDriveQueue)
};

struct Launcher {
	std::string title;
	std::string path;      // relative to the gamebox
	std::string arguments;
	bool isDefault = false;
};

enum class IdentifierType { UserSpecified = 0, UUID = 1, EXEDigest = 2, ReverseDNS = 3 };

class Gamebox {
public:
	static bool isGameboxPath(const std::string &path);

	bool open(const std::string &path, std::string *error = nullptr);

	const std::string &path() const { return path_; }
	// BXGamebox gameName: the file name, minus ".boxer" only (".dosbox"
	// and other extensions stay, as they may be part of the title).
	std::string gameName() const;

	std::string gameInfoPath() const;
	const PlistValue &gameInfo() const { return info_; }
	PlistValue &gameInfo() { return info_; }
	// Through fsutil::replaceFile; the gamebox may be read-only.
	bool saveGameInfo(std::string *error = nullptr);

	// BXGameIdentifier; empty when absent.
	std::string identifier() const;
	IdentifierType identifierType() const;
	// When the identifier is missing, generates one and records it in the
	// game info. Only type 1 (random UUID, upper-case CFUUID format) is
	// implemented; type 2 (SHA1 of the first 64 KiB of each meaningful EXE)
	// is deferred. Writing the plist back may fail on a read-only games
	// folder: the identifier is then kept in memory only and *persisted is
	// false; the caller decides what to do (a non-persisted identifier
	// changes every session, which orphans state).
	std::string ensureIdentifier(bool *persisted = nullptr, std::string *error = nullptr);

	// BXLaunchers, or one launcher built from the legacy BXDefaultProgramPath
	// ("Launch <gameName>", as _populateLaunchers does; not persisted here).
	std::vector<Launcher> launchers() const;
	std::string legacyDefaultProgramPath() const;
	bool closeAfterDefaultProgram() const;

	// Floppies, hard disks and CDs at the top level of the gamebox (hidden
	// files skipped), the root as C if none is called C, sorted by letter
	// then file name, as in -[BXGamebox bundledDrives]. Drives whose name
	// carries no letter get one by type (see the .cpp); a second drive on
	// an occupied letter is marked queued.
	std::vector<BundledDrive> bundledDrives() const;

	std::string configurationFilePath() const; // "DOSBox Preferences.conf"
	bool hasConfigurationFile() const;
	std::string documentationPath() const;     // "Documentation"
	bool hasDocumentationFolder() const;

private:
	std::string path_;
	PlistValue info_ = PlistValue::dict();
};

std::string generateUUID();

// --- session ---

// configurationURLsForEmulator: Preflight.conf (required), the game profile's
// confs (none yet: TODO game profile detection, BXGameProfile), the
// gamebox's DOSBox Preferences.conf if present (optional), Launch.conf
// (required). configDir holds Boxer's Configurations folder.
std::vector<ConfigFile> sessionConfigFiles(const std::string &configDir, const Gamebox *gamebox,
                                           const std::vector<std::string> &profileConfs = {});

// Quoting for DOSBox 0.74's CommandLine (misc/setup.cpp): a word starting
// with '"' runs to the next '"', with no escape. Returns false for a value
// that cannot be expressed (contains '"').
bool dosQuote(const std::string &value, std::string &out);

// MOUNT / IMGMOUNT lines for the preflight queue, one per non-queued drive,
// in bundledDrives order. Folders: MOUNT <L> "<path>" [-t cdrom|floppy]
// -label "<label>"; images: IMGMOUNT <L> "<path>" -t iso|floppy (0.74's
// IMGMOUNT ignores -label). Drives that cannot be quoted are skipped and
// named in *skipped.
std::vector<std::string> mountCommands(const std::vector<BundledDrive> &drives,
                                       std::vector<std::string> *skipped = nullptr);

} // namespace boxer
