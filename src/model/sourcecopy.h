// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXImportSession.m importSourceFiles / finalizeGamebox,
// BXImportSession+BXImportPolicies.m), https://github.com/alunbestor/Boxer at
// commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Copying a pre-installed game's folder into the new gamebox's C drive
// (IS:1004-1037, BXImportFromPreInstalledGame), in small steps so the UI can
// show progress and stop between them.
//
// AROS: one dos-level copy loop instead of ADBFileTransfer; junk files
// (IP:121-140) are not copied rather than copied and then deleted
// (cleanGamebox), with the same result. The original's C-drive replacement
// for a root-level game ("import the source as a new C drive") is a copy
// into the existing empty C.harddisk, which yields the same layout.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace boxer {

// +shouldUseSubfolderForSourceFilesAtURL: (IP:264): top level only, hidden
// entries skipped; a telltale means "installed at the root" (no subfolder);
// otherwise any executable means a subfolder.
bool shouldUseSubfolderForSource(const std::string &sourcePath);
// +validDOSNameFromName: lower-case, only [a-z0-9.], 8.3 cut. "" possible.
std::string validDOSName(const std::string &name);

class SourceCopy {
public:
	// Lists everything to copy below source (hidden and junk entries left
	// out) into destination, which must exist. False with a message when the
	// source cannot be read.
	bool prepare(const std::string &source, const std::string &destination, std::string *error = nullptr);
	// Copies up to about `budget` bytes (at least one entry). False on a
	// failure; error then names the file and the reason.
	bool step(uint64_t budget, std::string *error = nullptr);

	bool finished() const { return next_ >= entries_.size() && !open_; }
	uint64_t totalBytes() const { return total_; }
	uint64_t copiedBytes() const { return done_; }
	size_t fileCount() const { return files_; }
	void close();
	~SourceCopy() { close(); }

	// Test hook: called before writing each chunk with the destination path
	// and bytes written to it so far; returning false simulates a write
	// failure (disk full).
	std::function<bool(const std::string &, uint64_t)> writeFault;

private:
	struct Entry { std::string rel; bool dir; uint64_t size; };
	std::string src_, dst_;
	std::vector<Entry> entries_;
	size_t next_ = 0, files_ = 0;
	uint64_t total_ = 0, done_ = 0, written_ = 0;
	FILE *in_ = nullptr, *out_ = nullptr;
	bool open_ = false;
	void collect(const std::string &rel, int depth);
};

// Sidecar icon ToolTypes (decision O10): find-the-gamebox aids.
std::vector<std::string> sidecarToolTypes(const std::string &identifier, const std::string &gameboxFileName);

} // namespace boxer
