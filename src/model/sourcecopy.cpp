// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXImportSession.m, BXImportSession+BXImportPolicies.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "sourcecopy.h"

#include "fsutil.h"
#include "installerscan.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace boxer {

bool shouldUseSubfolderForSource(const std::string &sourcePath)
{
	std::vector<std::string> names;
	if (!fsutil::list(sourcePath, names))
		return false;
	bool hasExecutables = false;
	for (const auto &n : names) {
		if (n.empty() || n[0] == '.')
			continue;
		if (isPlayableGameTelltale(n))
			return false;
		const std::string ext = fsutil::extension(n);
		if (ext == "exe" || ext == "com" || ext == "bat")
			hasExecutables = true;
	}
	return hasExecutables;
}

std::string validDOSName(const std::string &name)
{
	std::string ascii;
	for (char c : fsutil::toLower(name))
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.')
			ascii += c;
	std::string base = fsutil::stripExtension(ascii), ext = fsutil::extension(ascii);
	if (base.size() > 8) base.resize(8);
	if (ext.size() > 3) ext.resize(3);
	return ext.empty() ? base : base + "." + ext;
}

void SourceCopy::collect(const std::string &rel, int depth)
{
	if (depth > 32)
		return;
	std::vector<std::string> names;
	if (!fsutil::list(rel.empty() ? src_ : fsutil::join(src_, rel), names))
		return;
	for (const auto &n : names) {
		if (n.empty() || n[0] == '.')
			continue;
		const std::string r = rel.empty() ? n : rel + "/" + n;
		const std::string full = fsutil::join(src_, r);
		if (isJunkImportPath(r))
			continue;
		if (fsutil::isDirectory(full)) {
			entries_.push_back({r, true, 0});
			collect(r, depth + 1);
		} else {
			struct stat st;
			const uint64_t size = ::stat(full.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0;
			entries_.push_back({r, false, size});
			total_ += size;
			++files_;
		}
	}
}

bool SourceCopy::prepare(const std::string &source, const std::string &destination, std::string *error)
{
	close();
	src_ = fsutil::trimTrailingSlash(source);
	dst_ = fsutil::trimTrailingSlash(destination);
	entries_.clear();
	next_ = files_ = 0;
	total_ = done_ = 0;
	if (!fsutil::isDirectory(src_)) {
		if (error) *error = source + " can no longer be read";
		return false;
	}
	if (!fsutil::isDirectory(dst_)) {
		if (error) *error = destination + " does not exist";
		return false;
	}
	collect("", 0);
	return true;
}

void SourceCopy::close()
{
	if (in_) std::fclose(in_);
	if (out_) std::fclose(out_);
	in_ = out_ = nullptr;
	open_ = false;
}

bool SourceCopy::step(uint64_t budget, std::string *error)
{
	uint64_t spent = 0;
	static char buf[64 * 1024];
	auto fail = [&](const std::string &what) {
		if (error) *error = what;
		close();
		return false;
	};
	do {
		if (!open_) {
			if (next_ >= entries_.size())
				return true;
			const Entry &e = entries_[next_];
			const std::string to = fsutil::join(dst_, e.rel);
			if (e.dir) {
				if (!fsutil::isDirectory(to) && ::mkdir(to.c_str(), 0755) != 0)
					return fail("Could not create the folder " + to + ": " + std::strerror(errno));
				++next_;
				continue;
			}
			in_ = std::fopen(fsutil::join(src_, e.rel).c_str(), "rb");
			if (!in_)
				return fail("Could not read " + fsutil::join(src_, e.rel) + ": " + std::strerror(errno));
			out_ = std::fopen(to.c_str(), "wb");
			if (!out_)
				return fail("Could not write " + to + ": " + std::strerror(errno));
			open_ = true;
			written_ = 0;
		}
		const Entry &e = entries_[next_];
		const std::string to = fsutil::join(dst_, e.rel);
		const size_t got = std::fread(buf, 1, sizeof buf, in_);
		if (got > 0) {
			if (writeFault && !writeFault(to, written_))
				return fail("Could not write " + to + ": disk full or write-protected");
			if (std::fwrite(buf, 1, got, out_) != got)
				return fail("Could not write " + to + ": " + std::strerror(errno));
			written_ += got;
			done_ += got;
			spent += got;
		}
		if (got < sizeof buf) {
			if (std::ferror(in_))
				return fail("Could not read " + fsutil::join(src_, e.rel));
			const bool closed = std::fclose(out_) == 0;
			out_ = nullptr;
			std::fclose(in_);
			in_ = nullptr;
			open_ = false;
			if (!closed)
				return fail("Could not write " + to + ": " + std::strerror(errno));
			++next_;
			if (spent == 0) spent = 1;   // empty files still make progress
		}
	} while (spent < budget);
	return true;
}

std::vector<std::string> sidecarToolTypes(const std::string &identifier, const std::string &gameboxFileName)
{
	return {"BOXERID=" + identifier, "GAMEBOX=" + gameboxFileName};
}

} // namespace boxer
