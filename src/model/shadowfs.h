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

// Write-shadowing filesystem for a gamebox's bundled drives: the port of
// ADBShadowedFilesystem (Other Sources/ADBToolkit/ADBShadowedFilesystem.m).
//
// Each mapping pairs a source root (a drive folder inside the gamebox) with
// a shadow root (".../Gamebox States/<id>/Current.boxerstate/<drive name>").
// The source is never written: a write first copies the original into the
// shadow (copy-on-write) and opens the copy; a deletion of an original
// leaves a 0-byte "<name>.deleted" marker in the shadow; listings merge
// source and shadow, minus deleted items and the markers themselves.
// "deleted" is seven characters, so a marker can never be a DOS 8.3 name.
// Paths outside every mapping go straight to the plain FileSystem.
#pragma once

#include "../emulator/filesystem.h"

#include <string>
#include <vector>

namespace boxer {

class ShadowFileSystem : public FileSystem {
public:
	static const char *const deletionMarkerExtension; // "deleted"

	// Source and shadow are host paths; the shadow need not exist yet.
	// readOnly refuses writes there (BXSession emulator:
	// shouldAllowWriteAccessToURL:onDrive: for read-only drives).
	void addMapping(const std::string &sourceRoot, const std::string &shadowRoot, bool readOnly = false);
	void addReadOnlyRoot(const std::string &root); // no shadow, writes refused
	void clearMappings() { mappings_.clear(); }

	FILE *open(const char *path, const char *mode, DOS_Drive *drive) override;
	bool remove(const char *path, DOS_Drive *drive) override;
	bool move(const char *from, const char *to, DOS_Drive *drive) override;
	bool makeDir(const char *path, DOS_Drive *drive) override;
	bool removeDir(const char *path, DOS_Drive *drive) override;
	bool stat(const char *path, DOS_Drive *drive, struct stat *out) override;
	bool dirExists(const char *path, DOS_Drive *drive) override;
	bool fileExists(const char *path, DOS_Drive *drive) override;
	void *openDir(const char *path, DOS_Drive *drive) override;
	bool nextEntry(void *handle, std::string &name, bool &isDirectory) override;
	void closeDir(void *handle) override;
	bool mayWrite(const char *path, DOS_Drive *drive) override;
	// BXSession emulator:shouldShowFileWithName: dot files and the gamebox's
	// metadata files are hidden.
	bool showFile(const char *name) override;

	// Where a logical path currently resolves (for tests and diagnostics):
	// the shadow copy if present, else the source; "" if deleted/absent.
	std::string resolvedPath(const std::string &path) const;

private:
	struct Mapping {
		std::string source, shadow;
		bool readOnly;
	};
	struct Resolved {
		const Mapping *m = nullptr;
		std::string rel, src, sh, marker;
	};

	Resolved resolve(const std::string &path) const;
	bool isDeleted(const Resolved &r) const;   // own marker or an ancestor's
	bool logicalExists(const Resolved &r, bool *isDir = nullptr) const;
	bool logicalParentIsDir(const Resolved &r) const;
	bool mergedList(const Resolved &r, std::vector<std::pair<std::string, bool>> &out) const;
	bool copyLogicalTree(const Resolved &from, const std::string &to) const;
	Resolved child(const Resolved &r, const std::string &name) const;
	static bool createMarker(const std::string &marker);
	bool removeLogical(const Resolved &r);

	std::vector<Mapping> mappings_;
};

} // namespace boxer
