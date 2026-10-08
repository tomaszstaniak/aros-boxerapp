// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "fsutil.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace boxer {
namespace fsutil {

static bool endsWithSeparator(const std::string &p)
{
	return !p.empty() && (p.back() == '/' || p.back() == ':');
}

std::string join(const std::string &base, const std::string &name)
{
	if (base.empty())
		return name;
	if (name.empty())
		return base;
	return endsWithSeparator(base) ? base + name : base + "/" + name;
}

std::string trimTrailingSlash(const std::string &path)
{
	std::string p = path;
	while (p.size() > 1 && p.back() == '/' && p[p.size() - 2] != ':')
		p.pop_back();
	return p;
}

std::string parent(const std::string &path)
{
	std::string p = trimTrailingSlash(path);
	size_t pos = p.find_last_of("/:");
	if (pos == std::string::npos)
		return "";
	if (p[pos] == ':')
		return p.substr(0, pos + 1);
	if (pos == 0)
		return "/";
	return p.substr(0, pos);
}

std::string baseName(const std::string &path)
{
	std::string p = trimTrailingSlash(path);
	size_t pos = p.find_last_of("/:");
	return pos == std::string::npos ? p : p.substr(pos + 1);
}

std::string toLower(std::string s)
{
	for (char &c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

std::string extension(const std::string &name)
{
	std::string b = baseName(name);
	size_t dot = b.rfind('.');
	if (dot == std::string::npos || dot == 0)
		return "";
	return toLower(b.substr(dot + 1));
}

std::string stripExtension(const std::string &name)
{
	size_t slash = name.find_last_of("/:");
	size_t dot = name.rfind('.');
	size_t start = slash == std::string::npos ? 0 : slash + 1;
	if (dot == std::string::npos || dot <= start)
		return name;
	return name.substr(0, dot);
}

static bool equalNoCase(const std::string &a, size_t n, const std::string &b)
{
	if (a.size() < n || b.size() < n)
		return false;
	for (size_t i = 0; i < n; i++)
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
			return false;
	return true;
}

bool isWithin(const std::string &path, const std::string &rootIn)
{
	std::string root = trimTrailingSlash(rootIn);
	std::string p = trimTrailingSlash(path);
	if (root.empty())
		return false;
	if (!equalNoCase(p, root.size(), root))
		return false;
	if (p.size() == root.size())
		return true;
	return endsWithSeparator(root) || p[root.size()] == '/';
}

std::string relativeTo(const std::string &path, const std::string &rootIn)
{
	std::string root = trimTrailingSlash(rootIn);
	std::string p = trimTrailingSlash(path);
	if (p.size() <= root.size())
		return "";
	std::string rel = p.substr(root.size());
	while (!rel.empty() && rel[0] == '/')
		rel.erase(0, 1);
	return rel;
}

bool exists(const std::string &path)
{
	struct stat st;
	return ::stat(path.c_str(), &st) == 0;
}

bool isDirectory(const std::string &path)
{
	struct stat st;
	return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string &path)
{
	struct stat st;
	return ::stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

bool makeDirs(const std::string &pathIn)
{
	std::string path = trimTrailingSlash(pathIn);
	if (path.empty() || isDirectory(path))
		return true;
	if (path.back() == ':')
		return isDirectory(path); // a volume cannot be created
	std::string up = parent(path);
	if (!up.empty() && up != path && !makeDirs(up))
		return false;
	return ::mkdir(path.c_str(), 0755) == 0 || isDirectory(path);
}

bool makeNewDir(const std::string &path, bool *existed)
{
	if (existed) *existed = false;
	if (::mkdir(trimTrailingSlash(path).c_str(), 0755) == 0)
		return true;
	// mkdir's EEXIST is not trusted across C libraries; the check after the
	// failed call is what tells "taken" from "cannot create".
	if (existed) *existed = exists(path);
	return false;
}

bool list(const std::string &dir, std::vector<std::string> &names)
{
	names.clear();
	DIR *d = opendir(dir.c_str());
	if (!d)
		return false;
	while (struct dirent *e = readdir(d)) {
		std::string n = e->d_name;
		if (n != "." && n != "..")
			names.push_back(n);
	}
	closedir(d);
	return true;
}

bool readFile(const std::string &path, std::string &data)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	data.clear();
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		data.append(buf, n);
	bool ok = !ferror(f);
	fclose(f);
	return ok;
}

bool writeFile(const std::string &path, const std::string &data)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = (fflush(f) == 0) && ok;
	ok = (fclose(f) == 0) && ok;
	return ok;
}

bool copyFile(const std::string &from, const std::string &to)
{
	FILE *in = fopen(from.c_str(), "rb");
	if (!in)
		return false;
	FILE *out = fopen(to.c_str(), "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	char buf[16384];
	size_t n;
	bool ok = true;
	while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0)
		ok = fwrite(buf, 1, n, out) == n;
	ok = ok && !ferror(in);
	fclose(in);
	ok = (fclose(out) == 0) && ok;
	if (!ok)
		::unlink(to.c_str());
	return ok;
}

bool copyTree(const std::string &from, const std::string &to)
{
	if (!isDirectory(from))
		return copyFile(from, to);
	if (!makeDirs(to))
		return false;
	std::vector<std::string> names;
	if (!list(from, names))
		return false;
	for (const auto &n : names)
		if (!copyTree(join(from, n), join(to, n)))
			return false;
	return true;
}

bool removeTree(const std::string &path)
{
	if (!isDirectory(path))
		return ::unlink(path.c_str()) == 0;
	std::vector<std::string> names;
	list(path, names);
	bool ok = true;
	for (const auto &n : names)
		ok = removeTree(join(path, n)) && ok;
	return ::rmdir(path.c_str()) == 0 && ok;
}

bool isWritableDirectory(const std::string &dir)
{
	if (!isDirectory(dir))
		return false;
	std::string probe = join(dir, ".boxer-write-probe");
	FILE *f = fopen(probe.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fputc('x', f) != EOF;
	ok = (fclose(f) == 0) && ok;
	ok = (::unlink(probe.c_str()) == 0) && ok;
	return ok;
}

// --- replacement ---

static ReplaceFaultHook faultHook;

void setReplaceFaultHook(ReplaceFaultHook hook) { faultHook = std::move(hook); }

static const char kNewSuffix[] = ".bxnew", kOldSuffix[] = ".bxold";
// A name that is taken (by someone else's file, or by an old version of
// ours that cannot be deleted yet) is skipped for a numbered one. Nine is
// far more than such leftovers can occupy; when all are taken the save
// fails and the previous version stays.
static const int kScratchNames = 9;

std::string tempPathFor(const std::string &path) { return path + kNewSuffix; }
std::string backupPathFor(const std::string &path) { return path + kOldSuffix; }

static std::string numbered(const std::string &first, int n)
{
	return n <= 1 ? first : first + "-" + std::to_string(n);
}

std::vector<std::string> ownedScratchPaths(const std::string &path)
{
	std::vector<std::string> out;
	for (int n = 1; n <= kScratchNames; n++)
		out.push_back(numbered(tempPathFor(path), n));
	for (int n = 1; n <= kScratchNames; n++)
		out.push_back(numbered(backupPathFor(path), n));
	return out;
}

// The scratch files this process made and has not removed yet. Only these
// are ever deleted or renamed back: a file that carries one of the names but
// was there before is someone else's (or left by a run that crashed, which
// cannot be told apart) and is never overwritten or removed.
static std::vector<std::string> createdList;
static std::vector<std::string> pendingList;

const std::vector<std::string> &pendingCleanup() { return pendingList; }

void forgetReplaceOwnership()
{
	createdList.clear();
	pendingList.clear();
}

static void setIn(std::vector<std::string> &list, const std::string &p, bool in)
{
	auto it = std::find(list.begin(), list.end(), p);
	if (in && it == list.end())
		list.push_back(p);
	else if (!in && it != list.end())
		list.erase(it);
}

static bool created(const std::string &p)
{
	return std::find(createdList.begin(), createdList.end(), p) != createdList.end();
}

// Removes one of this process's scratch files; remembered for later when the
// filesystem refuses (the file is in use). Anything else is left alone.
static bool removeOwned(const std::string &p)
{
	if (!created(p)) {
		setIn(pendingList, p, false);
		return true;
	}
	if (!exists(p)) {
		setIn(createdList, p, false);
		setIn(pendingList, p, false);
		return true;
	}
	const bool ok = ::unlink(p.c_str()) == 0;
	setIn(pendingList, p, !ok);
	if (ok)
		setIn(createdList, p, false);
	return ok;
}

std::vector<std::string> retryPendingCleanup()
{
	const std::vector<std::string> todo = pendingList;
	for (const auto &p : todo)
		removeOwned(p);
	return pendingList;
}

static std::string freeName(const std::string &first)
{
	for (int n = 1; n <= kScratchNames; n++)
		if (!exists(numbered(first, n)))
			return numbered(first, n);
	return std::string();
}

static FaultAction fault(ReplaceStep s)
{
	return faultHook ? faultHook(s) : FaultAction::Proceed;
}

static bool fail(std::string *error, const std::string &msg)
{
	if (error)
		*error = msg;
	return false;
}

static long modifiedTime(const std::string &p)
{
	struct stat st;
	return ::stat(p.c_str(), &st) == 0 ? (long)st.st_mtime : 0;
}

// Of the existing plain files among names, the newest (a renamed earlier
// version keeps its date); "" when there is none.
static std::string newestFile(const std::vector<std::string> &names)
{
	std::string pick;
	for (const auto &n : names)
		if (isFile(n) && (pick.empty() || modifiedTime(n) >= modifiedTime(pick)))
			pick = n;
	return pick;
}

bool recoverReplace(const std::string &path)
{
	std::vector<std::string> ours, others;
	for (int n = 1; n <= kScratchNames; n++) {
		const std::string b = numbered(backupPathFor(path), n);
		if (!isFile(b))
			continue;
		(created(b) ? ours : others).push_back(b);
	}
	bool ok = true;
	if (!exists(path)) {
		// Interrupted between moving the old version aside and installing
		// the new one.
		const std::string mine = newestFile(ours);
		if (!mine.empty()) {
			ok = ::rename(mine.c_str(), path.c_str()) == 0;
			if (ok) {
				setIn(createdList, mine, false);
				setIn(pendingList, mine, false);
				ours.erase(std::find(ours.begin(), ours.end(), mine));
			}
		} else if (!others.empty()) {
			// Most likely an earlier run that stopped at that point, but it
			// cannot be told from someone else's file: the previous version
			// comes back as a copy and the file itself stays where it is.
			ok = copyFile(newestFile(others), path);
		}
	}
	// With the file in place every backup of ours is older than it.
	if (exists(path))
		for (const auto &b : ours)
			removeOwned(b);
	// A temp of ours was never committed; whether complete or not, it is
	// not trusted.
	for (int n = 1; n <= kScratchNames; n++)
		removeOwned(numbered(tempPathFor(path), n));
	return ok;
}

bool replaceFile(const std::string &path, const std::string &data, std::string *error)
{
	retryPendingCleanup();
	if (!recoverReplace(path))
		return fail(error, "cannot restore " + path + " from an earlier interrupted save");
	const std::string tmp = freeName(tempPathFor(path));
	if (tmp.empty())
		return fail(error, "cannot write the new version of " + path + ": " + tempPathFor(path) +
		                       " and its numbered alternatives are all in use");

	FaultAction a = fault(ReplaceStep::WriteTemp);
	if (a == FaultAction::Crash)
		return fail(error, "simulated crash");
	setIn(createdList, tmp, true);
	if (a == FaultAction::Fail || !writeFile(tmp, data)) {
		removeOwned(tmp);
		return fail(error, "cannot write " + tmp);
	}

	const bool hadOld = exists(path);
	std::string bak;
	if (hadOld) {
		bak = freeName(backupPathFor(path));
		if (bak.empty()) {
			removeOwned(tmp);
			return fail(error, "cannot move " + path + " aside: " + backupPathFor(path) +
			                       " and its numbered alternatives are all in use");
		}
		a = fault(ReplaceStep::BackupOld);
		if (a == FaultAction::Crash)
			return fail(error, "simulated crash");
		if (a == FaultAction::Fail || ::rename(path.c_str(), bak.c_str()) != 0) {
			removeOwned(tmp);
			return fail(error, "cannot move " + path + " aside");
		}
		setIn(createdList, bak, true);
	}

	a = fault(ReplaceStep::RenameTemp);
	if (a == FaultAction::Crash)
		return fail(error, "simulated crash");
	if (a == FaultAction::Fail || ::rename(tmp.c_str(), path.c_str()) != 0) {
		removeOwned(tmp);
		if (hadOld) {
			if (::rename(bak.c_str(), path.c_str()) != 0)
				return fail(error, "cannot install new " + path + " and the previous version stays at " + bak);
			setIn(createdList, bak, false);
		}
		return fail(error, "cannot install new " + path + "; previous version kept");
	}
	setIn(createdList, tmp, false);

	if (hadOld) {
		a = fault(ReplaceStep::RemoveBackup);
		if (a == FaultAction::Crash)
			return fail(error, "simulated crash");
		// The new version is in place; an old one that cannot go yet is
		// retried later, not reported as a failed save.
		if (a == FaultAction::Fail)
			setIn(pendingList, bak, true);
		else
			removeOwned(bak);
	}
	return true;
}

} // namespace fsutil
} // namespace boxer
