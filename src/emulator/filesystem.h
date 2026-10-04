// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXCoalface.h local-file hooks),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host file access for DOS drives, behind the core's local-file hooks
// (boxer_openLocalFile & co. in Boxer/BXCoalface.h).
//
// The original routes these through ADBToolkit filesystems so that a
// gamebox's own files stay untouched and the game's writes land in
// "Gamebox States/<id>/Current.boxerstate" (ADBShadowedFilesystem). The
// plain implementation here passes straight through; the shadowing one is
// a separate class with the same interface.
#pragma once

#include <cstdio>
#include <string>
#include <sys/stat.h>

class DOS_Drive;

namespace boxer {

class FileSystem {
public:
	virtual ~FileSystem() = default;
	virtual FILE *open(const char *path, const char *mode, DOS_Drive *drive);
	virtual bool remove(const char *path, DOS_Drive *drive);
	virtual bool move(const char *from, const char *to, DOS_Drive *drive);
	virtual bool makeDir(const char *path, DOS_Drive *drive);
	virtual bool removeDir(const char *path, DOS_Drive *drive);
	virtual bool stat(const char *path, DOS_Drive *drive, struct stat *out);
	virtual bool dirExists(const char *path, DOS_Drive *drive);
	virtual bool fileExists(const char *path, DOS_Drive *drive);
	// Directory iteration; the handle is owned by the implementation.
	virtual void *openDir(const char *path, DOS_Drive *drive);
	virtual bool nextEntry(void *handle, std::string &name, bool &isDirectory);
	virtual void closeDir(void *handle);
	// Policy hooks (BXEmulator+BXDOSFileSystem in the original).
	virtual bool mayMount(const char *path) { return true; }
	virtual bool mayWrite(const char *path, DOS_Drive *drive) { return true; }
	virtual bool showFile(const char *name) { return true; }
};

} // namespace boxer
