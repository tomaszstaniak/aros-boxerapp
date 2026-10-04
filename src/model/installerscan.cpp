// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXInstallerScan.m, BXImportSession+BXImportPolicies.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "installerscan.h"

#include "fsutil.h"
#include "programs.h"
#include "../emulator/filesystem.h"

#include <algorithm>
#include <regex>

namespace boxer {

namespace {

using std::regex;
const auto icase = regex::ECMAScript | regex::icase;

// IP:30-39. Matched against the lower-cased file name, unanchored.
const char *const kInstallerPatterns[] = {"inst", "setup", "config"};
// IP:41-51, in priority order, caseless.
const char *const kPreferredInstallerPatterns[] = {"^dosinst", "^install\\.", "^hdinstal\\.", "^setup\\."};
// IP:64-96, caseless, against the relative path.
const char *const kIgnoredPatterns[] = {
	"(^|/)directx", "(^|/)acrodos", "(^|/)acroread\\.exe$", "(^|/)uvconfig\\.exe$", "(^|/)univbe",
	"(^|/)unins000\\.", "(^|/)Graphic mode setup\\.exe$", "(^|/)gogwrap\\.exe$", "(^|/)dosbox(.*)/",
	"(^|/)autorun", "(^|/)bootdisk\\.", "(^|/)readme\\.", "(^|/)foo\\.bat", "(^|/)vinstall\\.bat",
	"(^|/)pkunzip\\.", "(^|/)pkunzjr\\.", "(^|/)arj\\.", "(^|/)lha\\.",
};
// IP:121-140.
const char *const kJunkPatterns[] = {
	"(^|/)dosbox", "(^|/)goggame(.*)\\.dll", "(^|/)unins000\\.", "(^|/)Graphic mode setup\\.exe$",
	"(^|/)gogwrap\\.exe$", "(^|/)innosetup_license.txt", "(^|/)gfw_high(.*)\\.ico$", "(^|/)support\\.ico$",
	"\\.pif$", "\\.conf$",
};
// IP:162-185.
const char *const kTelltaleExtensions[] = {"conf", "iso", "cue", "cdr", "inst", "harddisk", "cdrom", "floppy"};
const char *const kTelltalePatterns[] = {"^gfw_high\\.ico$"};

template <size_t N>
bool anySearch(const char *const (&patterns)[N], const std::string &s, regex::flag_type flags)
{
	for (const char *p : patterns)
		if (std::regex_search(s, regex(p, flags)))
			return true;
	return false;
}

std::string fileNameOf(const std::string &rel)
{
	const size_t slash = rel.find_last_of('/');
	return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

// Same order as scanExecutables (programs.cpp): case-insensitive per folder.
bool lessNoCase(const std::string &a, const std::string &b) { return fsutil::toLower(a) < fsutil::toLower(b); }

size_t depthOf(const std::string &rel) { return std::count(rel.begin(), rel.end(), '/'); }

} // namespace

bool isInstallerPath(const std::string &rel)
{
	return anySearch(kInstallerPatterns, fsutil::toLower(fileNameOf(rel)), regex::ECMAScript);
}

bool isIgnoredImportPath(const std::string &rel) { return anySearch(kIgnoredPatterns, rel, icase); }

bool isJunkImportPath(const std::string &rel) { return anySearch(kJunkPatterns, rel, icase); }

bool isPlayableGameTelltale(const std::string &rel)
{
	const std::string name = fsutil::toLower(fileNameOf(rel));
	const std::string ext = fsutil::extension(name);
	for (const char *e : kTelltaleExtensions)
		if (ext == e)
			return true;
	return anySearch(kTelltalePatterns, name, regex::ECMAScript);
}

bool isInconclusiveDOSProgram(const std::string &rel)
{
	return fsutil::extension(fileNameOf(rel)) == "bat";
}

std::string preferredInstaller(const std::vector<std::string> &paths)
{
	for (const char *p : kPreferredInstallerPatterns) {
		const regex re(p, icase);
		for (const auto &path : paths)
			if (std::regex_search(fileNameOf(path), re))
				return path;
	}
	return "";
}

void sortByPathDepth(std::vector<std::string> &paths)
{
	std::stable_sort(paths.begin(), paths.end(),
	                 [](const std::string &a, const std::string &b) { return depthOf(a) < depthOf(b); });
}

static void walk(FileSystem &fs, const std::string &root, const std::string &rel, InstallerScanResult &r, int depth)
{
	if (depth > 32)
		return;
	const std::string dir = rel.empty() ? root : fsutil::join(root, rel);
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
		// ADBFileScan skips hidden files.
		if (e.first.empty() || e.first[0] == '.' || !fs.showFile(e.first.c_str()))
			continue;
		const std::string childRel = rel.empty() ? e.first : rel + "/" + e.first;
		if (isIgnoredImportPath(e.second ? childRel + "/" : childRel))
			continue;
		// Telltales include folder names such as "C.harddisk".
		if (!r.alreadyInstalled && isPlayableGameTelltale(childRel))
			r.alreadyInstalled = true;
		if (e.second) {
			walk(fs, root, childRel, r, depth + 1);
			continue;
		}
		const std::string full = fsutil::join(root, childRel);
		const std::string ext = fsutil::extension(e.first);
		if (ext == "conf")
			r.dosboxConfigurations.push_back(childRel);
		if (ext != "exe" && ext != "com" && ext != "bat")
			continue;
		if (isCompatibleExecutable(fs, full)) {
			r.dosExecutables.push_back(childRel);
			if (isInstallerPath(childRel))
				r.installers.push_back(childRel);
		} else {
			r.windowsExecutables.push_back(childRel);
		}
	}
}

InstallerScanResult scanForInstallers(FileSystem &fs, const std::string &root)
{
	InstallerScanResult r;
	walk(fs, root, "", r, 0);

	bool conclusivelyDOS = !r.dosExecutables.empty();
	if (conclusivelyDOS && !r.windowsExecutables.empty()) {
		conclusivelyDOS = false;
		for (const auto &p : r.dosExecutables)
			if (!isInconclusiveDOSProgram(p)) {
				conclusivelyDOS = true;
				break;
			}
	}
	if (conclusivelyDOS) {
		sortByPathDepth(r.installers);
		const std::string preferred = preferredInstaller(r.installers);
		if (!preferred.empty()) {
			r.installers.erase(std::find(r.installers.begin(), r.installers.end(), preferred));
			r.installers.insert(r.installers.begin(), preferred);
		}
	} else if (r.dosboxConfigurations.empty()) {
		// No profiles on AROS, so the "already installed and known game"
		// benefit of the doubt never applies.
		r.error = r.windowsExecutables.empty() ? InstallerScanError::NoExecutables
		                                       : InstallerScanError::WindowsOnly;
	}
	return r;
}

std::string installerScanErrorText(InstallerScanError error, const std::string &sourceName)
{
	const std::string q = "\"" + sourceName + "\"";
	switch (error) {
	case InstallerScanError::NoExecutables:
		return q + " does not contain any MS-DOS programs.\n\n"
		           "This folder may contain a game for another platform which is not supported by Boxer.";
	case InstallerScanError::WindowsOnly:
		return q + " is a Windows game. Boxer only supports MS-DOS games.";
	case InstallerScanError::None:
		break;
	}
	return "";
}

} // namespace boxer
