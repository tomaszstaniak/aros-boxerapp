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
// One undeletable old version must not block the next save, so a backup may
// take a numbered name. Nine is far more than a reader holding a file for a
// moment can occupy; when all are taken the save fails and the previous
// version stays.
static const int kBackupNames = 9;

std::string tempPathFor(const std::string &path) { return path + kNewSuffix; }
std::string backupPathFor(const std::string &path) { return path + kOldSuffix; }

static std::string backupName(const std::string &path, int n)
{
	return n <= 1 ? backupPathFor(path) : backupPathFor(path) + "-" + std::to_string(n);
}

std::vector<std::string> ownedScratchPaths(const std::string &path)
{
	std::vector<std::string> out{tempPathFor(path)};
	for (int n = 1; n <= kBackupNames; n++)
		out.push_back(backupName(path, n));
	return out;
}

static std::vector<std::string> pendingList;

const std::vector<std::string> &pendingCleanup() { return pendingList; }

static void setPending(const std::string &p, bool pending)
{
	auto it = std::find(pendingList.begin(), pendingList.end(), p);
	if (pending && it == pendingList.end())
		pendingList.push_back(p);
	else if (!pending && it != pendingList.end())
		pendingList.erase(it);
}

// Only a plain file can be one of ours: a drawer that happens to carry the
// name was made by someone else, and DeleteFile would remove it if empty.
static bool isOwnedFile(const std::string &p) { return isFile(p); }

// Removes one of this code's scratch files; remembered for later when the
// filesystem refuses (the file is in use).
static bool removeOwned(const std::string &p)
{
	if (!isOwnedFile(p)) {
		setPending(p, false);
		return true;
	}
	const bool ok = ::unlink(p.c_str()) == 0;
	setPending(p, !ok);
	return ok;
}

std::vector<std::string> retryPendingCleanup()
{
	const std::vector<std::string> todo = pendingList;
	for (const auto &p : todo)
		removeOwned(p);
	return pendingList;
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

bool recoverReplace(const std::string &path)
{
	std::vector<std::string> backups;
	for (int n = 1; n <= kBackupNames; n++)
		if (isOwnedFile(backupName(path, n)))
			backups.push_back(backupName(path, n));
	bool ok = true;
	if (!exists(path) && !backups.empty()) {
		// Interrupted between moving the old version aside and installing
		// the new one. Each backup is a renamed earlier version and keeps its
		// date, so the newest is the version the interrupted save replaced.
		size_t pick = 0;
		for (size_t i = 1; i < backups.size(); i++)
			if (modifiedTime(backups[i]) >= modifiedTime(backups[pick]))
				pick = i;
		ok = ::rename(backups[pick].c_str(), path.c_str()) == 0;
		if (ok) {
			setPending(backups[pick], false);
			backups.erase(backups.begin() + (long)pick);
		}
	}
	// With the file in place every backup is an older version than it.
	if (exists(path))
		for (const auto &b : backups)
			removeOwned(b);
	// A leftover temp was never committed; whether complete or not, it is
	// not trusted.
	removeOwned(tempPathFor(path));
	return ok;
}

bool replaceFile(const std::string &path, const std::string &data, std::string *error)
{
	retryPendingCleanup();
	if (!recoverReplace(path))
		return fail(error, "cannot restore " + path + " from an earlier interrupted save");
	const std::string tmp = tempPathFor(path);
	if (exists(tmp))
		return fail(error, "cannot write " + tmp + ": the name is in use");

	FaultAction a = fault(ReplaceStep::WriteTemp);
	if (a == FaultAction::Crash)
		return fail(error, "simulated crash");
	if (a == FaultAction::Fail || !writeFile(tmp, data)) {
		::unlink(tmp.c_str());
		return fail(error, "cannot write " + tmp);
	}

	const bool hadOld = exists(path);
	std::string bak;
	if (hadOld) {
		for (int n = 1; n <= kBackupNames && bak.empty(); n++)
			if (!exists(backupName(path, n)))
				bak = backupName(path, n);
		if (bak.empty()) {
			::unlink(tmp.c_str());
			return fail(error, "cannot move " + path + " aside: " + backupPathFor(path) +
			                       " and its numbered alternatives are all in use");
		}
		a = fault(ReplaceStep::BackupOld);
		if (a == FaultAction::Crash)
			return fail(error, "simulated crash");
		if (a == FaultAction::Fail || ::rename(path.c_str(), bak.c_str()) != 0) {
			::unlink(tmp.c_str());
			return fail(error, "cannot move " + path + " aside");
		}
	}

	a = fault(ReplaceStep::RenameTemp);
	if (a == FaultAction::Crash)
		return fail(error, "simulated crash");
	if (a == FaultAction::Fail || ::rename(tmp.c_str(), path.c_str()) != 0) {
		::unlink(tmp.c_str());
		if (hadOld && ::rename(bak.c_str(), path.c_str()) != 0)
			return fail(error, "cannot install new " + path + " and the previous version stays at " + bak);
		return fail(error, "cannot install new " + path + "; previous version kept");
	}

	if (hadOld) {
		a = fault(ReplaceStep::RemoveBackup);
		if (a == FaultAction::Crash)
			return fail(error, "simulated crash");
		// The new version is in place; an old one that cannot go yet is
		// retried later, not reported as a failed save.
		if (a == FaultAction::Fail)
			setPending(bak, true);
		else
			removeOwned(bak);
	}
	return true;
}

} // namespace fsutil
} // namespace boxer
