// SPDX-License-Identifier: BSD-2-Clause
// Derived from ADBToolkit's ADBShadowedFilesystem (Boxer, Other Sources/ADBToolkit,
// https://github.com/alunbestor/Boxer at commit 0062fc18):
//   Copyright (c) 2013, Alun Bestor (alun.bestor@gmail.com)
//   All rights reserved.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Copyright (C) 2026 Tomasz Staniak (modifications).
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
//   Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "shadowfs.h"

#include "fsutil.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace boxer {

const char *const ShadowFileSystem::deletionMarkerExtension = "deleted";

namespace {

struct Listing {
	std::vector<std::pair<std::string, bool>> entries;
	size_t next = 0;
};

bool isMarkerName(const std::string &name)
{
	return fsutil::extension(name) == ShadowFileSystem::deletionMarkerExtension && name.size() > 8;
}

bool sameName(const std::string &a, const std::string &b) { return fsutil::toLower(a) == fsutil::toLower(b); }

} // namespace

void ShadowFileSystem::addMapping(const std::string &sourceRoot, const std::string &shadowRoot, bool readOnly)
{
	mappings_.push_back({fsutil::trimTrailingSlash(sourceRoot), fsutil::trimTrailingSlash(shadowRoot), readOnly});
}

void ShadowFileSystem::addReadOnlyRoot(const std::string &root)
{
	mappings_.push_back({fsutil::trimTrailingSlash(root), "", true});
}

ShadowFileSystem::Resolved ShadowFileSystem::resolve(const std::string &path) const
{
	Resolved r;
	// Longest source root wins: a legacy gamebox's root is drive C and
	// may contain other drive folders.
	for (const auto &m : mappings_)
		if (fsutil::isWithin(path, m.source) && (!r.m || m.source.size() > r.m->source.size()))
			r.m = &m;
	if (!r.m)
		return r;
	r.rel = fsutil::relativeTo(path, r.m->source);
	r.src = r.rel.empty() ? r.m->source : fsutil::join(r.m->source, r.rel);
	if (!r.m->shadow.empty()) {
		r.sh = r.rel.empty() ? r.m->shadow : fsutil::join(r.m->shadow, r.rel);
		r.marker = r.rel.empty() ? "" : r.sh + "." + deletionMarkerExtension;
	}
	return r;
}

ShadowFileSystem::Resolved ShadowFileSystem::child(const Resolved &r, const std::string &name) const
{
	return resolve(fsutil::join(r.src, name));
}

bool ShadowFileSystem::isDeleted(const Resolved &r) const
{
	if (r.sh.empty() || r.rel.empty())
		return false;
	std::string rel = r.rel;
	for (;;) {
		if (fsutil::exists(fsutil::join(r.m->shadow, rel) + "." + deletionMarkerExtension))
			return true;
		size_t slash = rel.rfind('/');
		if (slash == std::string::npos)
			return false;
		rel.erase(slash);
	}
}

bool ShadowFileSystem::logicalExists(const Resolved &r, bool *isDir) const
{
	if (isDeleted(r))
		return false;
	const std::string &p = !r.sh.empty() && fsutil::exists(r.sh) ? r.sh : r.src;
	if (!fsutil::exists(p))
		return false;
	if (isDir)
		*isDir = fsutil::isDirectory(p);
	return true;
}

bool ShadowFileSystem::logicalParentIsDir(const Resolved &r) const
{
	if (r.rel.empty())
		return true;
	size_t slash = r.rel.rfind('/');
	if (slash == std::string::npos)
		return true; // the drive root
	Resolved p = resolve(fsutil::join(r.m->source, r.rel.substr(0, slash)));
	bool dir = false;
	return logicalExists(p, &dir) && dir;
}

std::string ShadowFileSystem::resolvedPath(const std::string &path) const
{
	Resolved r = resolve(path);
	if (!r.m)
		return fsutil::exists(path) ? path : "";
	if (isDeleted(r))
		return "";
	if (!r.sh.empty() && fsutil::exists(r.sh))
		return r.sh;
	return fsutil::exists(r.src) ? r.src : "";
}

bool ShadowFileSystem::createMarker(const std::string &marker)
{
	fsutil::makeDirs(fsutil::parent(marker));
	return fsutil::writeFile(marker, "");
}

FILE *ShadowFileSystem::open(const char *path, const char *mode, DOS_Drive *drive)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::open(path, mode, drive);
	bool writing = strpbrk(mode, "wa+") != nullptr;
	bool truncate = strchr(mode, 'w') != nullptr;
	bool create = strpbrk(mode, "wa") != nullptr;
	if (r.sh.empty()) {
		if (writing) {
			errno = EACCES;
			return nullptr;
		}
		return fopen(r.src.c_str(), mode);
	}

	if (isDeleted(r)) {
		// Only the item's own marker can be lifted; a deleted ancestor
		// means the parent directory does not exist.
		if (!create || !fsutil::exists(r.marker) || !logicalParentIsDir(r)) {
			errno = ENOENT;
			return nullptr;
		}
		::unlink(r.marker.c_str());
		fsutil::removeTree(r.sh);
		fsutil::makeDirs(fsutil::parent(r.sh));
		return fopen(r.sh.c_str(), mode);
	}
	if (fsutil::exists(r.sh))
		return fopen(r.sh.c_str(), mode);
	if (!writing)
		return fopen(r.src.c_str(), mode);

	if (!logicalParentIsDir(r)) {
		errno = ENOENT;
		return nullptr;
	}
	if (fsutil::isDirectory(r.src)) {
		errno = EISDIR;
		return nullptr;
	}
	fsutil::makeDirs(fsutil::parent(r.sh));
	if (!truncate) {
		bool copied = fsutil::copyFile(r.src, r.sh);
		if (!copied && !create) {
			errno = ENOENT;
			return nullptr;
		}
	}
	return fopen(r.sh.c_str(), mode);
}

bool ShadowFileSystem::removeLogical(const Resolved &r)
{
	if (r.sh.empty())
		return false;
	if (isDeleted(r))
		return false;
	if (fsutil::exists(r.src)) {
		if (!createMarker(r.marker))
			return false;
		if (fsutil::exists(r.sh))
			fsutil::removeTree(r.sh);
		return true;
	}
	::unlink(r.marker.c_str());
	return fsutil::exists(r.sh) && fsutil::removeTree(r.sh);
}

bool ShadowFileSystem::remove(const char *path, DOS_Drive *drive)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::remove(path, drive);
	return removeLogical(r);
}

// As in the original, a directory goes with its contents: ADB's
// removeItemAtPath: is recursive and Boxer's DOSBox routes RD there without
// an emptiness check (localDrive::RemoveDir -> boxer_removeLocalDir).
bool ShadowFileSystem::removeDir(const char *path, DOS_Drive *drive)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::removeDir(path, drive);
	bool dir = false;
	if (r.rel.empty() || !logicalExists(r, &dir) || !dir)
		return false;
	return removeLogical(r);
}

bool ShadowFileSystem::makeDir(const char *path, DOS_Drive *drive)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::makeDir(path, drive);
	if (r.sh.empty() || r.rel.empty())
		return false;
	if (fsutil::exists(r.src)) {
		if (!fsutil::exists(r.marker))
			return false; // exists already (or an ancestor is deleted)
		if (!logicalParentIsDir(r))
			return false;
		// Recreated over a deleted original: it must look empty, so every
		// original child is marked deleted (createDirectoryAtPath:).
		::unlink(r.marker.c_str());
		fsutil::removeTree(r.sh);
		if (!fsutil::makeDirs(r.sh))
			return false;
		std::vector<std::string> names;
		if (fsutil::isDirectory(r.src) && fsutil::list(r.src, names))
			for (const auto &n : names)
				createMarker(fsutil::join(r.sh, n) + "." + deletionMarkerExtension);
		return true;
	}
	if (logicalExists(r) || !logicalParentIsDir(r))
		return false;
	if (!fsutil::makeDirs(r.sh))
		return false;
	::unlink(r.marker.c_str());
	return true;
}

bool ShadowFileSystem::copyLogicalTree(const Resolved &from, const std::string &to) const
{
	bool dir = false;
	if (!logicalExists(from, &dir))
		return false;
	if (!dir) {
		const std::string &p = !from.sh.empty() && fsutil::exists(from.sh) ? from.sh : from.src;
		return fsutil::copyFile(p, to);
	}
	if (!fsutil::makeDirs(to))
		return false;
	std::vector<std::pair<std::string, bool>> list;
	if (!mergedList(from, list))
		return false;
	for (const auto &e : list)
		if (!copyLogicalTree(child(from, e.first), fsutil::join(to, e.first)))
			return false;
	return true;
}

// _transferItemAtPath:toPath:copying:NO. One deliberate difference: a
// directory is copied as the merged view. The original copied the shadowed
// directory if one existed and lost the unmodified originals inside it.
bool ShadowFileSystem::move(const char *fromPath, const char *toPath, DOS_Drive *drive)
{
	Resolved from = resolve(fromPath), to = resolve(toPath);
	if (!from.m && !to.m)
		return FileSystem::move(fromPath, toPath, drive);
	if (!to.m || to.sh.empty() || (from.m && from.sh.empty() && from.m->readOnly))
		return false; // into an unmapped tree, or out of a read-only drive
	if (from.m && isDeleted(from))
		return false;
	if (!logicalParentIsDir(to))
		return false;

	fsutil::makeDirs(fsutil::parent(to.sh));
	std::string staging = to.sh + ".boxer-move";
	fsutil::removeTree(staging);
	bool ok;
	if (from.m)
		ok = copyLogicalTree(from, staging);
	else
		ok = fsutil::copyTree(fromPath, staging);
	if (!ok) {
		fsutil::removeTree(staging);
		return false;
	}
	if (fsutil::exists(to.sh))
		fsutil::removeTree(to.sh);
	if (::rename(staging.c_str(), to.sh.c_str()) != 0) {
		fsutil::removeTree(staging);
		return false;
	}
	if (from.m) {
		if (!from.sh.empty() && fsutil::exists(from.sh))
			fsutil::removeTree(from.sh);
		if (fsutil::exists(from.src))
			createMarker(from.marker);
	} else {
		fsutil::removeTree(fromPath);
	}
	::unlink(to.marker.c_str());
	// The destination replaces whatever original was at that name; its
	// original children must not show through a moved-in directory.
	if (fsutil::isDirectory(to.src) && fsutil::isDirectory(to.sh)) {
		std::vector<std::string> names;
		fsutil::list(to.src, names);
		for (const auto &n : names) {
			bool present = fsutil::exists(fsutil::join(to.sh, n));
			if (!present)
				createMarker(fsutil::join(to.sh, n) + "." + deletionMarkerExtension);
		}
	}
	return true;
}

bool ShadowFileSystem::stat(const char *path, DOS_Drive *drive, struct stat *out)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::stat(path, drive, out);
	std::string p = resolvedPath(path);
	return !p.empty() && ::stat(p.c_str(), out) == 0;
}

bool ShadowFileSystem::dirExists(const char *path, DOS_Drive *drive)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::dirExists(path, drive);
	bool dir = false;
	return logicalExists(r, &dir) && dir;
}

bool ShadowFileSystem::fileExists(const char *path, DOS_Drive *drive)
{
	Resolved r = resolve(path);
	if (!r.m)
		return FileSystem::fileExists(path, drive);
	bool dir = true;
	return logicalExists(r, &dir) && !dir;
}

// ADBShadowedDirectoryEnumerator, one level: shadow entries first (markers
// recorded and hidden), then originals that are neither shadowed nor deleted.
bool ShadowFileSystem::mergedList(const Resolved &r, std::vector<std::pair<std::string, bool>> &out) const
{
	out.clear();
	bool dir = false;
	if (!logicalExists(r, &dir) || !dir)
		return false;
	std::vector<std::string> shadowNames, sourceNames, deleted;
	if (!r.sh.empty() && fsutil::isDirectory(r.sh)) {
		fsutil::list(r.sh, shadowNames);
		for (const auto &n : shadowNames) {
			if (isMarkerName(n))
				deleted.push_back(fsutil::stripExtension(n));
			else
				out.emplace_back(n, fsutil::isDirectory(fsutil::join(r.sh, n)));
		}
	}
	// A shadow directory created over a deleted original hides the original
	// through child markers, so the source can always be merged in.
	if (fsutil::isDirectory(r.src)) {
		fsutil::list(r.src, sourceNames);
		for (const auto &n : sourceNames) {
			auto same = [&](const std::string &o) { return sameName(o, n); };
			if (std::any_of(deleted.begin(), deleted.end(), same))
				continue;
			if (std::any_of(out.begin(), out.end(), [&](const std::pair<std::string, bool> &e) { return same(e.first); }))
				continue;
			out.emplace_back(n, fsutil::isDirectory(fsutil::join(r.src, n)));
		}
	}
	return true;
}

void *ShadowFileSystem::openDir(const char *path, DOS_Drive *drive)
{
	auto *l = new Listing;
	Resolved r = resolve(path);
	if (r.m) {
		if (!mergedList(r, l->entries)) {
			delete l;
			return nullptr;
		}
		return l;
	}
	std::vector<std::string> names;
	if (!fsutil::list(path, names)) {
		delete l;
		return nullptr;
	}
	for (const auto &n : names)
		l->entries.emplace_back(n, fsutil::isDirectory(fsutil::join(path, n)));
	return l;
}

bool ShadowFileSystem::nextEntry(void *handle, std::string &name, bool &isDirectory)
{
	auto *l = static_cast<Listing *>(handle);
	if (l->next >= l->entries.size())
		return false;
	name = l->entries[l->next].first;
	isDirectory = l->entries[l->next].second;
	l->next++;
	return true;
}

void ShadowFileSystem::closeDir(void *handle) { delete static_cast<Listing *>(handle); }

bool ShadowFileSystem::mayWrite(const char *path, DOS_Drive *)
{
	Resolved r = resolve(path);
	return !r.m || !r.m->readOnly;
}

bool ShadowFileSystem::showFile(const char *nameIn)
{
	std::string name = nameIn;
	if (name == "." || name == "..")
		return true;
	if (!name.empty() && name[0] == '.')
		return false;
	// BXSession hiddenFilenamePatterns.
	return name != "DOSBox Preferences.conf" && name != "Game Info.plist" && name != "DOSBox Target" &&
	       name != "Icon\r";
}

} // namespace boxer
