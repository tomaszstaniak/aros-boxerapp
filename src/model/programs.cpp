// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXFileTypes.m, BXExecutableConstants.h,
// BXSession.m, BXSession+BXFileManagement.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.
#include "programs.h"

#include "../emulator/filesystem.h"
#include "datalocations.h"
#include "fsutil.h"
#include "gamebox.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace boxer {

const char *const kRecentProgramsKey = "BXGameRecentPrograms";
const char *const kShowLaunchPanelKey = "showLaunchPanel";
const char *const kAlwaysShowLaunchPanelKey = "alwaysShowLaunchPanel";

namespace {

// BXExecutableConstants.h
const uint16_t kMZ = 0x5A4D, kNE = 0x454E, kPE = 0x4550, kLE = 0x454C, kLX = 0x584C,
               kW3 = 0x3357, kW4 = 0x3457;
const uint16_t kExtendedRelocationAddress = 0x40;
const long kNEHeaderLength = 64, kPEHeaderLength = 24;
const long kMaxWarningStubLength = 3584;
const size_t kHeaderSize = 64; // sizeof(BXDOSExecutableHeader), packed

uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool lessNoCase(const std::string &a, const std::string &b)
{
	return fsutil::toLower(a) < fsutil::toLower(b);
}

} // namespace

ExecutableType executableType(FileSystem &fs, const std::string &path)
{
	FILE *f = fs.open(path.c_str(), "rb", nullptr);
	if (!f)
		return ExecutableType::Unknown;
	unsigned char h[kHeaderSize];
	size_t got = std::fread(h, 1, sizeof h, f);
	ExecutableType type = ExecutableType::Unknown;
	if (got < sizeof h || le16(h) != kMZ) {
		// Truncated header or no MZ marker: not an executable.
	} else if (le16(h + 24) != kExtendedRelocationAddress || le32(h + 60) == 0) {
		type = ExecutableType::DOS;
	} else {
		const long newHeader = (long)le32(h + 60);
		unsigned char m[2];
		if (std::fseek(f, newHeader, SEEK_SET) != 0) {
			type = ExecutableType::Unknown; // seek failure is a read error there
		} else if (std::fread(m, 1, 2, f) < 2) {
			type = ExecutableType::DOS;    // marker beyond the end of the file
		} else {
			const uint16_t marker = le16(m);
			std::fseek(f, 0, SEEK_END);
			const long size = std::ftell(f);
			switch (marker) {
			case kNE:
			case kPE: {
				if (newHeader > kMaxWarningStubLength) { type = ExecutableType::DOS; break; }
				const long minLength = marker == kPE ? kPEHeaderLength : kNEHeaderLength;
				type = size < newHeader + minLength ? ExecutableType::DOS : ExecutableType::Windows;
				break;
			}
			case kLE:
			case kLX:
				type = newHeader > kMaxWarningStubLength ? ExecutableType::DOS : ExecutableType::OS2;
				break;
			case kW3:
			case kW4:
				type = ExecutableType::Windows;
				break;
			default:
				type = ExecutableType::DOS;
			}
		}
	}
	std::fclose(f);
	return type;
}

bool isCompatibleExecutable(FileSystem &fs, const std::string &path)
{
	const std::string ext = fsutil::extension(fsutil::baseName(path));
	if (ext == "com" || ext == "bat")
		return true;
	if (ext == "exe")
		return executableType(fs, path) == ExecutableType::DOS;
	return false;
}

static void scanInto(FileSystem &fs, const std::string &dir, std::vector<std::string> &out, int depth)
{
	if (depth > 32)
		return;
	void *h = fs.openDir(dir.c_str(), nullptr);
	if (!h)
		return;
	std::vector<std::pair<std::string, bool>> entries;
	std::string name;
	bool isDir = false;
	while (fs.nextEntry(h, name, isDir))
		entries.emplace_back(name, isDir);
	fs.closeDir(h);
	std::sort(entries.begin(), entries.end(),
	          [](const std::pair<std::string, bool> &a, const std::pair<std::string, bool> &b) {
		          return lessNoCase(a.first, b.first);
	          });
	for (const auto &e : entries) {
		if (e.first.empty() || e.first[0] == '.' || !fs.showFile(e.first.c_str()))
			continue;
		const std::string path = fsutil::join(dir, e.first);
		if (e.second) {
			// "Don't scan nested drives" (old-style gameboxes with the root as C).
			if (drivetypes::isMountableFolderName(e.first))
				continue;
			scanInto(fs, path, out, depth + 1);
		} else if (isCompatibleExecutable(fs, path)) {
			out.push_back(path);
		}
	}
}

std::vector<std::string> scanExecutables(FileSystem &fs, const std::string &root)
{
	std::vector<std::string> out;
	scanInto(fs, fsutil::trimTrailingSlash(root), out, 0);
	return out;
}

// --- game settings ---

std::string GameSettings::pathFor(const DataLocations &where, const std::string &identifier)
{
	return fsutil::join(fsutil::join(where.dataDir, "Game Settings"), safeFolderName(identifier) + ".plist");
}

bool GameSettings::load(const std::string &path, std::string *error)
{
	path_ = path;
	dict_ = PlistValue::dict();
	fsutil::recoverReplace(path);
	if (!fsutil::exists(path)) {
		const std::string left = fsutil::describeLeftovers(path);
		if (left.empty())
			return true;
		if (error) *error = left;
		return false;
	}
	PlistValue v;
	if (!readPlistFile(path, v, error))
		return false;
	if (v.type() != PlistValue::Type::Dict) {
		if (error) *error = path + ": not a dictionary";
		return false;
	}
	dict_ = v;
	return true;
}

bool GameSettings::save(std::string *error) const
{
	if (path_.empty()) {
		if (error) *error = "no settings path";
		return false;
	}
	const std::string dir = fsutil::parent(path_);
	if (!fsutil::isDirectory(dir) && !fsutil::makeDirs(dir)) {
		if (error) *error = "cannot create " + dir;
		return false;
	}
	return writePlistFile(path_, dict_, error);
}

bool GameSettings::flag(const std::string &key) const
{
	const PlistValue *v = dict_.get(key);
	return v && v->asBool(false);
}

void GameSettings::setFlag(const std::string &key, bool value)
{
	dict_.set(key, PlistValue::boolean(value));
}

std::vector<RecentProgram> GameSettings::recentPrograms() const
{
	std::vector<RecentProgram> out;
	const PlistValue *list = dict_.get(kRecentProgramsKey);
	if (!list || list->type() != PlistValue::Type::Array)
		return out;
	for (const auto &item : list->items()) {
		const PlistValue *p = item.get("path");
		if (!p || p->type() != PlistValue::Type::String || p->str().empty())
			continue;
		RecentProgram r;
		r.path = p->str();
		const PlistValue *a = item.get("arguments");
		if (a && a->type() == PlistValue::Type::String)
			r.arguments = a->str();
		out.push_back(r);
	}
	return out;
}

void GameSettings::setRecentPrograms(const std::vector<RecentProgram> &programs)
{
	// _saveGameSettings: the key is removed when the list is empty.
	if (programs.empty()) {
		dict_.remove(kRecentProgramsKey);
		return;
	}
	PlistValue list = PlistValue::array();
	for (const auto &r : programs) {
		PlistValue item = PlistValue::dict();
		item.set("path", PlistValue::string(r.path));
		if (!r.arguments.empty())
			item.set("arguments", PlistValue::string(r.arguments));
		list.items().push_back(item);
	}
	dict_.set(kRecentProgramsKey, list);
}

bool samePath(const std::string &a, const std::string &b)
{
	return fsutil::toLower(a) == fsutil::toLower(b);
}

void removeRecentProgram(std::vector<RecentProgram> &list, const RecentProgram &program)
{
	list.erase(std::remove_if(list.begin(), list.end(),
	                          [&](const RecentProgram &r) {
		                          return samePath(r.path, program.path) && r.arguments == program.arguments;
	                          }),
	           list.end());
}

void noteRecentProgram(std::vector<RecentProgram> &list, const RecentProgram &program)
{
	removeRecentProgram(list, program);
	list.insert(list.begin(), program);
	while (list.size() > kRecentProgramsLimit)
		list.pop_back();
}

} // namespace boxer
