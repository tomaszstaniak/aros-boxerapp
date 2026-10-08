// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXGamebox.m, BXDrive.m, BXFileTypes.m, BXSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "gamebox.h"

#include "fsutil.h"
#include "../emulator/emulator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <random>
#include <ctime>
#include <memory>

namespace boxer {

namespace {
// Keys from BXGamebox.m.
const char *const kIdentifier = "BXGameIdentifier";
const char *const kIdentifierType = "BXGameIdentifierType";
const char *const kDefaultProgram = "BXDefaultProgramPath";
const char *const kLaunchers = "BXLaunchers";
const char *const kCloseAfter = "BXCloseAfterDefaultProgram";
const char *const kLauncherTitle = "BXLauncherTitle";
const char *const kLauncherPath = "BXLauncherPath";
const char *const kLauncherArgs = "BXLauncherArguments";
const char *const kLauncherDefault = "BXLauncherIsDefault";

bool inList(const std::string &ext, std::initializer_list<const char *> list)
{
	for (const char *e : list)
		if (ext == e)
			return true;
	return false;
}
} // namespace

namespace drivetypes {

bool isGameboxName(const std::string &name)
{
	return inList(fsutil::extension(name), {"boxer", "dosbox"});
}

bool isMountableFolderName(const std::string &name)
{
	return inList(fsutil::extension(name), {"harddisk", "harddrive", "cdrom", "floppy"});
}

// mountableImageTypes: disk bundle (.cdmedia), ISO, CDR, cue sheet
// (.cue/.inst), raw floppy (.ima), VirtualPC (.vfd), NDIF (.img).
bool isMountableImageName(const std::string &name)
{
	return inList(fsutil::extension(name), {"cdmedia", "iso", "cdr", "cue", "inst", "ima", "vfd", "img"});
}

bool volumeType(const std::string &name, DriveType &type)
{
	std::string e = fsutil::extension(name);
	if (inList(e, {"harddisk", "harddrive"})) { type = DriveType::HardDisk; return true; }
	if (inList(e, {"cue", "inst", "cdrom", "cdmedia", "iso", "cdr"})) { type = DriveType::CDROM; return true; }
	if (inList(e, {"floppy", "ima", "img", "vfd"})) { type = DriveType::Floppy; return true; }
	return false;
}

} // namespace drivetypes

char preferredDriveLetter(const std::string &name)
{
	if (!drivetypes::isMountableFolderName(name) && !drivetypes::isMountableImageName(name))
		return 0;
	std::string base = fsutil::stripExtension(fsutil::baseName(name));
	// ^([a-xA-X])( .*)?$
	if (base.empty())
		return 0;
	char c = (char)std::toupper((unsigned char)base[0]);
	if (c < 'A' || c > 'X')
		return 0;
	if (base.size() == 1 || base[1] == ' ')
		return c;
	return 0;
}

std::string preferredVolumeLabel(const std::string &nameIn)
{
	std::string base = fsutil::baseName(nameIn);
	// mountableTypesWithExtensions: images, mountable folders, gameboxes.
	if (drivetypes::isMountableFolderName(base) || drivetypes::isMountableImageName(base) ||
	    drivetypes::isGameboxName(base))
		base = fsutil::stripExtension(base);
	// " (\(\d+\))$"
	if (base.size() >= 4 && base.back() == ')') {
		size_t open = base.rfind(" (");
		if (open != std::string::npos && open + 3 < base.size()) {
			bool digits = true;
			for (size_t i = open + 2; i + 1 < base.size(); i++)
				digits = digits && std::isdigit((unsigned char)base[i]);
			if (digits)
				base.erase(open);
		}
	}
	// "^([a-xA-X] )?(.+)$": the prefix only goes when something follows it.
	if (base.size() >= 3 && base[1] == ' ') {
		char c = (char)std::toupper((unsigned char)base[0]);
		if (c >= 'A' && c <= 'X')
			base.erase(0, 2);
	}
	return base;
}

// --- Gamebox ---

bool Gamebox::isGameboxPath(const std::string &path)
{
	return drivetypes::isGameboxName(fsutil::baseName(path)) && fsutil::isDirectory(path);
}

bool Gamebox::open(const std::string &pathIn, std::string *error)
{
	std::string p = fsutil::trimTrailingSlash(pathIn);
	if (!fsutil::isDirectory(p)) {
		if (error) *error = p + " is not a directory";
		return false;
	}
	if (!drivetypes::isGameboxName(fsutil::baseName(p))) {
		if (error) *error = p + " is not a .boxer or .dosbox gamebox";
		return false;
	}
	path_ = p;
	info_ = PlistValue::dict();
	// A gamebox without Game Info is valid (an older one), but not when an
	// interrupted save left its earlier versions: going on would give the
	// game a new identifier and separate it from its saved state.
	fsutil::recoverReplace(gameInfoPath());
	const std::string left = fsutil::describeLeftovers(gameInfoPath());
	if (!left.empty()) {
		if (error) *error = left;
		return false;
	}
	if (fsutil::exists(gameInfoPath())) {
		PlistValue v;
		std::string e;
		if (!readPlistFile(gameInfoPath(), v, &e) || v.type() != PlistValue::Type::Dict) {
			if (error) *error = e.empty() ? gameInfoPath() + ": root is not a dictionary" : e;
			return false;
		}
		info_ = v;
	}
	return true;
}

std::string Gamebox::gameName() const
{
	std::string n = fsutil::baseName(path_);
	if (fsutil::extension(n) == "boxer")
		n = fsutil::stripExtension(n);
	return n;
}

std::string Gamebox::gameInfoPath() const { return fsutil::join(path_, "Game Info.plist"); }

bool Gamebox::saveGameInfo(std::string *error) { return writePlistFile(gameInfoPath(), info_, error); }

std::string Gamebox::identifier() const
{
	const PlistValue *v = info_.get(kIdentifier);
	return v && v->type() == PlistValue::Type::String ? v->str() : std::string();
}

IdentifierType Gamebox::identifierType() const
{
	const PlistValue *v = info_.get(kIdentifierType);
	return (IdentifierType)(v ? v->asInteger(0) : 0);
}

std::string generateUUID()
{
	// Seeded from several sources. std::random_device throws "device not
	// available" in the AROS libstdc++ (seen on ABIv11 2026.09,
	// 2026-10-02), so it is optional; clocks, a process-wide counter and
	// stack/heap addresses keep identifiers distinct within and across runs.
	// Not cryptographic, as the original's CFUUID need not be either.
	static unsigned counter = 0;
	unsigned extra[2] = {0, 0};
	try {
		std::random_device rd;
		extra[0] = rd();
		extra[1] = rd();
	} catch (const std::exception &) {
	}
	const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const auto steady = std::chrono::steady_clock::now().time_since_epoch().count();
	std::unique_ptr<int> heap(new int(0));
	std::seed_seq seq{extra[0], extra[1], (unsigned)now, (unsigned)(now >> 32), (unsigned)steady,
	                  (unsigned)(steady >> 32), (unsigned)(uintptr_t)&counter, (unsigned)(uintptr_t)heap.get(),
	                  (unsigned)(uintptr_t)&extra, ++counter, (unsigned)time(nullptr)};
	std::mt19937_64 gen(seq);
	unsigned char b[16];
	for (int i = 0; i < 16; i += 8) {
		uint64_t r = gen();
		for (int j = 0; j < 8; j++)
			b[i + j] = (unsigned char)(r >> (j * 8));
	}
	b[6] = (unsigned char)((b[6] & 0x0F) | 0x40); // version 4
	b[8] = (unsigned char)((b[8] & 0x3F) | 0x80); // RFC 4122 variant
	char s[37];
	snprintf(s, sizeof s, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
	         b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
	return s;
}

std::string Gamebox::ensureIdentifier(bool *persisted, std::string *error)
{
	std::string id = identifier();
	if (!id.empty()) {
		if (persisted) *persisted = true;
		return id;
	}
	// TODO: type 2 (EXE digest) as -[BXGamebox _generatedIdentifierOfType:].
	id = generateUUID();
	info_.set(kIdentifier, PlistValue::string(id));
	info_.set(kIdentifierType, PlistValue::integer((int)IdentifierType::UUID));
	bool ok = saveGameInfo(error);
	if (persisted) *persisted = ok;
	return id;
}

std::string Gamebox::legacyDefaultProgramPath() const
{
	const PlistValue *v = info_.get(kDefaultProgram);
	return v && v->type() == PlistValue::Type::String ? v->str() : std::string();
}

bool Gamebox::closeAfterDefaultProgram() const
{
	const PlistValue *v = info_.get(kCloseAfter);
	return v && v->asBool();
}

std::vector<Launcher> Gamebox::launchers() const
{
	std::vector<Launcher> out;
	const PlistValue *list = info_.get(kLaunchers);
	if (list && list->type() == PlistValue::Type::Array && !list->items().empty()) {
		for (const auto &item : list->items()) {
			if (item.type() != PlistValue::Type::Dict)
				continue;
			Launcher l;
			if (const PlistValue *v = item.get(kLauncherPath)) l.path = v->str();
			if (l.path.empty())
				continue;
			if (const PlistValue *v = item.get(kLauncherTitle)) l.title = v->str();
			if (const PlistValue *v = item.get(kLauncherArgs)) l.arguments = v->str();
			if (const PlistValue *v = item.get(kLauncherDefault)) l.isDefault = v->asBool();
			if (l.title.empty())
				l.title = fsutil::baseName(l.path);
			out.push_back(l);
		}
		return out;
	}
	// TODO: the pre-1.4 "DOSBox Target" symlink (legacyTargetURL) is not read.
	std::string legacy = legacyDefaultProgramPath();
	if (!legacy.empty()) {
		Launcher l;
		l.title = "Launch " + gameName();
		l.path = legacy;
		out.push_back(l);
	}
	return out;
}

std::vector<BundledDrive> Gamebox::bundledDrives() const
{
	std::vector<std::string> names;
	fsutil::list(path_, names);
	std::vector<BundledDrive> drives;
	bool hasC = false;
	// Floppies, then hard disks, then CDs, as the original collects them;
	// the final sort makes the order matter only between equal keys.
	for (DriveType want : {DriveType::Floppy, DriveType::HardDisk, DriveType::CDROM}) {
		for (const auto &n : names) {
			DriveType t;
			if (n.empty() || n[0] == '.' || !drivetypes::volumeType(n, t) || t != want)
				continue;
			BundledDrive d;
			d.sourcePath = fsutil::join(path_, n);
			std::string ext = fsutil::extension(n);
			d.isImage = drivetypes::isMountableImageName(n);
			d.mountPath = ext == "cdmedia" ? fsutil::join(d.sourcePath, "tracks.cue") : d.sourcePath;
			d.type = t;
			d.letter = preferredDriveLetter(n);
			d.label = preferredVolumeLabel(n);
			d.shadowName = n;
			if (d.letter == 'C')
				hasC = true;
			drives.push_back(d);
		}
	}
	if (!hasC) {
		BundledDrive d;
		d.sourcePath = d.mountPath = path_;
		d.letter = 'C';
		d.label = preferredVolumeLabel(fsutil::baseName(path_));
		d.type = DriveType::HardDisk;
		d.isGameboxRoot = true;
		d.shadowName = "C.harddisk";
		drives.push_back(d);
	}
	// NSSortDescriptor on letter then file name; a nil letter sorts first.
	std::stable_sort(drives.begin(), drives.end(), [](const BundledDrive &a, const BundledDrive &b) {
		if (a.letter != b.letter)
			return a.letter < b.letter;
		return fsutil::baseName(a.sourcePath) < fsutil::baseName(b.sourcePath);
	});

	// mountDrive:ifExists:BXDriveQueue options:BXBundledDriveMountOptions
	// (KeepWithSameType | AvoidAssigningDriveC), drive by drive.
	std::vector<char> used;
	auto isUsed = [&](char c) { return std::find(used.begin(), used.end(), c) != used.end(); };
	for (auto &d : drives) {
		if (!d.letter) {
			if (d.type != DriveType::HardDisk) {
				for (const auto &o : drives)
					if (&o != &d && o.letter && o.type == d.type && isUsed(o.letter)) {
						d.letter = o.letter;
						break;
					}
			}
			if (!d.letter) {
				char first = d.type == DriveType::Floppy ? 'A' : d.type == DriveType::CDROM ? 'D' : 'C';
				char last = d.type == DriveType::CDROM ? 'Y' : 'X';
				for (char c = first; c <= last && !d.letter; c++)
					if (c != 'C' && !isUsed(c))
						d.letter = c;
			}
			if (!d.letter)
				continue; // out of letters: the original fails this mount
		}
		if (isUsed(d.letter))
			d.queued = true;
		else
			used.push_back(d.letter);
	}
	return drives;
}

std::string Gamebox::configurationFilePath() const { return fsutil::join(path_, "DOSBox Preferences.conf"); }
bool Gamebox::hasConfigurationFile() const { return fsutil::isFile(configurationFilePath()); }
std::string Gamebox::documentationPath() const { return fsutil::join(path_, "Documentation"); }
bool Gamebox::hasDocumentationFolder() const { return fsutil::isDirectory(documentationPath()); }

// --- session ---

std::vector<ConfigFile> sessionConfigFiles(const std::string &configDir, const Gamebox *gamebox,
                                           const std::vector<std::string> &profileConfs)
{
	std::vector<ConfigFile> out;
	out.push_back({fsutil::join(configDir, "Preflight.conf"), true});
	// TODO: game profile detection (BXGameProfile, BXSession
	// profileForGameAtURL:); until then callers pass none.
	for (const auto &name : profileConfs)
		out.push_back({fsutil::join(configDir, name + ".conf"), true});
	if (gamebox && gamebox->hasConfigurationFile())
		out.push_back({gamebox->configurationFilePath(), false});
	out.push_back({fsutil::join(configDir, "Launch.conf"), true});
	return out;
}

bool dosQuote(const std::string &value, std::string &out)
{
	if (value.find('"') != std::string::npos)
		return false;
	bool plain = !value.empty();
	for (char c : value)
		if (c == ' ' || c == '\t' || c == '<' || c == '>' || c == '|')
			plain = false;
	out = plain ? value : "\"" + value + "\"";
	return true;
}

std::vector<std::string> mountCommands(const std::vector<BundledDrive> &drives, std::vector<std::string> *skipped)
{
	std::vector<std::string> out;
	for (const auto &d : drives) {
		if (d.queued || !d.letter)
			continue;
		std::string path, label;
		std::string mp = d.mountPath;
		// "Ensure that folder paths have a trailing slash, otherwise DOSBox
		// will get shirty" (BXEmulator mountDrive:).
		if (!d.isImage && !mp.empty() && mp.back() != '/' && mp.back() != ':')
			mp += '/';
		if (!dosQuote(mp, path)) {
			if (skipped) skipped->push_back(d.sourcePath);
			continue;
		}
		std::string line;
		if (d.isImage) {
			line = std::string("IMGMOUNT ") + d.letter + " " + path + " -t " +
			       (d.type == DriveType::Floppy ? "floppy" : "iso");
		} else {
			line = std::string("MOUNT ") + d.letter + " " + path;
			if (d.type == DriveType::CDROM) line += " -t cdrom";
			else if (d.type == DriveType::Floppy) line += " -t floppy";
			std::string clean;
			for (char c : d.label)
				if (c != '"') clean += c;
			if (!clean.empty() && dosQuote(clean, label))
				line += " -label " + label;
		}
		out.push_back(line);
	}
	return out;
}

} // namespace boxer
