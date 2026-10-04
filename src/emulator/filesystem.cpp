// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXCoalface.mm local-file hooks),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "filesystem.h"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>

namespace boxer {

FILE *FileSystem::open(const char *path, const char *mode, DOS_Drive *) { return fopen(path, mode); }
bool FileSystem::remove(const char *path, DOS_Drive *) { return ::unlink(path) == 0; }
bool FileSystem::move(const char *from, const char *to, DOS_Drive *) { return ::rename(from, to) == 0; }
bool FileSystem::makeDir(const char *path, DOS_Drive *) { return ::mkdir(path, 0755) == 0; }
bool FileSystem::removeDir(const char *path, DOS_Drive *) { return ::rmdir(path) == 0; }
bool FileSystem::stat(const char *path, DOS_Drive *, struct stat *out) { return ::stat(path, out) == 0; }

bool FileSystem::dirExists(const char *path, DOS_Drive *)
{
	struct stat st;
	return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool FileSystem::fileExists(const char *path, DOS_Drive *)
{
	struct stat st;
	return ::stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

namespace {
struct Directory {
	DIR *dir;
	std::string path;
};
}

void *FileSystem::openDir(const char *path, DOS_Drive *)
{
	DIR *dir = opendir(path);
	return dir ? new Directory{dir, path} : nullptr;
}

// "." and ".." are skipped here; the hook layer supplies them itself.
bool FileSystem::nextEntry(void *handle, std::string &name, bool &isDirectory)
{
	auto *d = static_cast<Directory *>(handle);
	while (struct dirent *entry = readdir(d->dir)) {
		name = entry->d_name;
		if (name == "." || name == "..")
			continue;
		// d_type is optional in POSIX; fall back to stat when unknown.
		if (entry->d_type != DT_UNKNOWN) {
			isDirectory = entry->d_type == DT_DIR;
		} else {
			std::string full = d->path;
			if (!full.empty() && full.back() != '/' && full.back() != ':')
				full += '/';
			isDirectory = dirExists((full + name).c_str(), nullptr);
		}
		return true;
	}
	return false;
}

void FileSystem::closeDir(void *handle)
{
	auto *d = static_cast<Directory *>(handle);
	closedir(d->dir);
	delete d;
}

} // namespace boxer
