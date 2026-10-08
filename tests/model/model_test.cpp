// Host unit tests for src/model. Fixtures are built at run time under a
// temporary directory whose names contain spaces, as gamebox names do.
// Usage: model_test <scratch dir>
#include "../../src/model/importsource.h"
#include "../../src/model/installerscan.h"
#include "../../src/model/sourcecopy.h"
#include "../../src/emulator/emulator.h"
#include "../../src/model/datalocations.h"
#include "../../src/model/fsutil.h"
#include "../../src/model/gamebox.h"
#include "../../src/model/plist.h"
#include "../../src/model/shadowfs.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

using namespace boxer;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; auto _a = (a); auto _b = (b); if (!(_a == _b)) { failures++; fprintf(stderr, "FAIL %s:%d: %s == %s\n  got:  [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, #b, toStr(_a).c_str(), toStr(_b).c_str()); } } while (0)

static std::string toStr(const std::string &s) { return s; }
static std::string toStr(char c) { return c ? std::string(1, c) : "(none)"; }
static std::string toStr(size_t n) { return std::to_string(n); }
static std::string toStr(const std::vector<std::string> &v)
{
	std::string o;
	for (const auto &s : v) o += "{" + s + "}";
	return o;
}

static std::string scratch;

static void put(const std::string &path, const std::string &data)
{
	fu::makeDirs(fu::parent(path));
	if (!fu::writeFile(path, data)) { fprintf(stderr, "cannot write fixture %s\n", path.c_str()); exit(2); }
}

static std::string get(const std::string &path)
{
	std::string d;
	return fu::readFile(path, d) ? d : "<missing>";
}

// Snapshot of a tree: relative path -> size, mtime and contents.
static void snapshot(const std::string &root, const std::string &rel, std::map<std::string, std::string> &out)
{
	std::string p = rel.empty() ? root : fu::join(root, rel);
	struct stat st;
	::stat(p.c_str(), &st);
	if (fu::isDirectory(p)) {
		out[rel + "/"] = "dir " + std::to_string((long long)st.st_mtime);
		std::vector<std::string> names;
		fu::list(p, names);
		for (const auto &n : names) snapshot(root, rel.empty() ? n : rel + "/" + n, out);
	} else {
		out[rel] = std::to_string((long long)st.st_size) + " " + std::to_string((long long)st.st_mtime) + " " + get(p);
	}
}

// --- plist ---

static void testPlist()
{
	const char *text =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n<dict>\n"
		"\t<key>BXDefaultProgramPath</key>\n\t<string>C.harddisk/XCOMDEMO/XCOM.BAT</string>\n"
		"\t<key>BXGameIdentifier</key>\n\t<string>644E7C</string>\n"
		"\t<!-- a comment -->\n"
		"\t<key>BXGameIdentifierType</key>\n\t<integer>2</integer>\n"
		"\t<key>Unknown &amp; kept</key>\n\t<array><real>1.5</real><true/><false/><date>2012-01-06T12:00:00Z</date>"
		"<data>SGVs\nbG8=</data><string/><dict/><array/></array>\n"
		"\t<key>Escaped</key>\n\t<string>&lt;a&gt; &quot;b&quot; &#x41;&#66; caf\xC3\xA9</string>\n"
		"</dict>\n</plist>\n";
	PlistValue v;
	std::string err;
	CHECK(parsePlist(text, v, &err));
	CHECK_EQ(err, std::string());
	CHECK(v.type() == PlistValue::Type::Dict);
	CHECK_EQ(v.get("BXGameIdentifier")->str(), std::string("644E7C"));
	CHECK(v.get("BXGameIdentifierType")->integer() == 2);
	const PlistValue *arr = v.get("Unknown & kept");
	CHECK(arr && arr->items().size() == 8);
	if (arr && arr->items().size() == 8) {
		CHECK(arr->items()[0].real() == 1.5);
		CHECK(arr->items()[1].boolean() && !arr->items()[2].boolean());
		CHECK_EQ(arr->items()[3].str(), std::string("2012-01-06T12:00:00Z"));
		CHECK_EQ(arr->items()[4].str(), std::string("Hello"));
		CHECK_EQ(arr->items()[5].str(), std::string(""));
	}
	CHECK_EQ(v.get("Escaped")->str(), std::string("<a> \"b\" AB caf\xC3\xA9"));

	// Round trip: write, parse, equal; key order preserved; second write identical.
	std::string out = writePlist(v);
	PlistValue back;
	CHECK(parsePlist(out, back, &err));
	CHECK(back == v);
	CHECK_EQ(writePlist(back), out);
	CHECK_EQ(back.entries()[0].first, std::string("BXDefaultProgramPath"));
	CHECK_EQ(back.entries()[3].first, std::string("Unknown & kept"));

	// Changing one key keeps the others and their positions.
	back.set("BXGameIdentifier", PlistValue::string("NEW"));
	PlistValue again;
	CHECK(parsePlist(writePlist(back), again));
	CHECK_EQ(again.entries()[1].first, std::string("BXGameIdentifier"));
	CHECK_EQ(again.get("BXGameIdentifier")->str(), std::string("NEW"));
	CHECK(*again.get("Unknown & kept") == *v.get("Unknown & kept"));

	// Binary data of every length round-trips through base64.
	for (size_t n = 0; n < 7; n++) {
		std::string bytes;
		for (size_t i = 0; i < n; i++) bytes += (char)(0xF0 + i);
		PlistValue d = PlistValue::dict();
		d.set("d", PlistValue::data(bytes));
		PlistValue r;
		CHECK(parsePlist(writePlist(d), r) && r.get("d")->str() == bytes);
	}

	// Rejections.
	err.clear();
	CHECK(!parsePlist(std::string("bplist00\x01\x02", 10), v, &err));
	CHECK(err.find("binary property list") != std::string::npos);
	CHECK(!parsePlist("<plist><dict><key>a</key></dict></plist>", v, &err));
	CHECK(!parsePlist("<plist><dict><key>a</key><integer>x</integer></dict></plist>", v, &err));
	CHECK(!parsePlist("{ a = b; }", v, &err));
}

// --- replace ---

static void setMTime(const std::string &path, time_t t)
{
	struct timeval tv[2] = {{t, 0}, {t, 0}};
	utimes(path.c_str(), tv);
}

// Makes a file undeletable the way a reader holding it does on AROS (DOS
// refuses to delete an object in use). Returns false where the host cannot.
static bool lockFile(const std::string &path, bool lock)
{
#ifdef UF_IMMUTABLE
	return chflags(path.c_str(), lock ? UF_IMMUTABLE : 0) == 0;
#else
	(void)path; (void)lock;
	return false;
#endif
}

// Files that already carry the replace's own names (.bxnew, .bxold and the
// numbered alternatives) but were not made by this run: someone else's, or
// left by a run that crashed. They are never overwritten, renamed or removed.
static void testReplaceForeignScratch(const std::string &dir)
{
	using S = fu::ReplaceStep;
	using A = fu::FaultAction;
	const std::string f = fu::join(dir, "Foreign Scratch.prefs");
	const std::string tmp = fu::tempPathFor(f), bak = fu::backupPathFor(f);
	put(f, "v1");
	put(tmp, "foreign new");
	put(bak, "foreign old");
	put(bak + "-2", "foreign old 2");
	auto foreignIntact = [&]() {
		return get(tmp) == "foreign new" && get(bak) == "foreign old" && get(bak + "-2") == "foreign old 2";
	};

	// Saves go to the next free names and clean up only those.
	CHECK(fu::replaceFile(f, "v2"));
	CHECK_EQ(get(f), std::string("v2"));
	CHECK(foreignIntact());
	CHECK(!fu::exists(tmp + "-2") && !fu::exists(bak + "-3"));
	CHECK(fu::recoverReplace(f));
	CHECK(foreignIntact());
	// A failed or interrupted save, and its recovery, leave them too.
	fu::setReplaceFaultHook([](S s) { return s == S::RenameTemp ? A::Fail : A::Proceed; });
	CHECK(!fu::replaceFile(f, "bad"));
	fu::setReplaceFaultHook([](S s) { return s == S::RemoveBackup ? A::Crash : A::Proceed; });
	CHECK(!fu::replaceFile(f, "v3"));
	fu::setReplaceFaultHook(nullptr);
	CHECK(fu::recoverReplace(f));
	CHECK_EQ(get(f), std::string("v3"));
	CHECK(foreignIntact());
	CHECK(!fu::exists(tmp + "-2") && !fu::exists(bak + "-3"));
	fu::setReplaceFaultHook([](S s) { return s == S::RenameTemp ? A::Crash : A::Proceed; });
	CHECK(!fu::replaceFile(f, "v4"));
	fu::setReplaceFaultHook(nullptr);
	CHECK(!fu::exists(f));
	CHECK(fu::recoverReplace(f));
	CHECK_EQ(get(f), std::string("v3"));
	CHECK(foreignIntact());
	CHECK(!fu::exists(tmp + "-2") && !fu::exists(bak + "-3"));

	// A new run finds the scratch files of a run that stopped half-way: they
	// cannot be told from someone else's. The file is missing, so the newest
	// earlier version comes back as a copy; nothing is removed.
	fu::setReplaceFaultHook([](S s) { return s == S::RenameTemp ? A::Crash : A::Proceed; });
	CHECK(!fu::replaceFile(f, "v5"));                   // v3 now at bak-3, v5 at tmp-2
	fu::setReplaceFaultHook(nullptr);
	setMTime(bak, 1000000000);
	setMTime(bak + "-2", 1000000000);
	setMTime(bak + "-3", 1100000000);
	fu::forgetReplaceOwnership();
	CHECK(fu::recoverReplace(f));
	CHECK_EQ(get(f), std::string("v3"));
	CHECK(foreignIntact());
	CHECK_EQ(get(bak + "-3"), std::string("v3"));
	CHECK_EQ(get(tmp + "-2"), std::string("v5"));
	// Later saves of the new run work around them.
	CHECK(fu::replaceFile(f, "v6"));
	CHECK_EQ(get(f), std::string("v6"));
	CHECK(foreignIntact());
	CHECK_EQ(get(bak + "-3"), std::string("v3"));
	CHECK_EQ(get(tmp + "-2"), std::string("v5"));
	CHECK(!fu::exists(bak + "-4") && !fu::exists(tmp + "-3"));

	// All names taken: the save fails and keeps the file as it is.
	std::vector<std::string> extra;
	for (int n = 1; n <= 9; n++) {
		const std::string t = n == 1 ? tmp : tmp + "-" + std::to_string(n);
		if (!fu::exists(t)) { put(t, "x"); extra.push_back(t); }
	}
	std::string err;
	CHECK(!fu::replaceFile(f, "v7", &err));
	CHECK(err.find("in use") != std::string::npos);
	CHECK_EQ(get(f), std::string("v6"));
	CHECK(foreignIntact());
	for (const auto &t : extra) CHECK_EQ(get(t), std::string("x"));
	CHECK(fu::pendingCleanup().empty());
}

// The replace only ever deletes its own scratch files, and an old version
// that cannot be deleted (a game icon Wanderer is still reading) neither
// fails the save nor stays behind for good.
static void testReplaceOwnership(const std::string &dir)
{
	using S = fu::ReplaceStep;
	using A = fu::FaultAction;
	const std::string icon = fu::join(dir, "The Long Named Game.info");
	const std::string bak = fu::backupPathFor(icon), bak2 = bak + "-2", tmp = fu::tempPathFor(icon);
	const std::string foreignBak = icon + ".bak", foreignTmp = icon + ".tmp";
	CHECK(bak != foreignBak && tmp != foreignTmp);
	put(foreignBak, "user's own copy");
	put(foreignTmp, "another program's file");
	put(icon, "icon 1");

	// Saves, a cleanup failure and its recovery leave the foreign files alone.
	CHECK(fu::replaceFile(icon, "icon 2"));
	fu::setReplaceFaultHook([](S s) { return s == S::RemoveBackup ? A::Fail : A::Proceed; });
	CHECK(fu::replaceFile(icon, "icon 3"));
	fu::setReplaceFaultHook(nullptr);
	CHECK(fu::recoverReplace(icon));
	CHECK(fu::replaceFile(icon, "icon 4"));
	CHECK_EQ(get(icon), std::string("icon 4"));
	CHECK_EQ(get(foreignBak), std::string("user's own copy"));
	CHECK_EQ(get(foreignTmp), std::string("another program's file"));
	CHECK(!fu::exists(bak) && !fu::exists(tmp));

	// A crash with no icon in place: the foreign ".bak" is not taken for
	// ours and not restored; ours is.
	fu::setReplaceFaultHook([](S s) { return s == S::RenameTemp ? A::Crash : A::Proceed; });
	CHECK(!fu::replaceFile(icon, "icon 5"));
	fu::setReplaceFaultHook(nullptr);
	CHECK(!fu::exists(icon));
	CHECK(fu::recoverReplace(icon));
	CHECK_EQ(get(icon), std::string("icon 4"));
	CHECK_EQ(get(foreignBak), std::string("user's own copy"));
	CHECK_EQ(get(foreignTmp), std::string("another program's file"));

	// A drawer carrying our backup name is someone else's: never deleted,
	// the save uses the next name.
	fu::makeDirs(bak);
	CHECK(fu::replaceFile(icon, "icon 6"));
	CHECK_EQ(get(icon), std::string("icon 6"));
	CHECK(fu::isDirectory(bak) && !fu::exists(bak2));
	::rmdir(bak.c_str());
	// A drawer on the temp name is skipped the same way.
	fu::makeDirs(tmp);
	CHECK(fu::replaceFile(icon, "icon 7"));
	CHECK_EQ(get(icon), std::string("icon 7"));
	CHECK(fu::isDirectory(tmp) && !fu::exists(tmp + "-2"));
	::rmdir(tmp.c_str());

	// The old version is in use when it is to be deleted.
	std::string lockedPath;
	bool lockedOnce = false;
	fu::setReplaceFaultHook([&](S s) {
		if (s == S::RemoveBackup && !lockedOnce) {
			lockedOnce = true;
			if (lockFile(bak, true)) lockedPath = bak;
			else return A::Fail;   // no file locking on this host: the hook stands in
		}
		return A::Proceed;
	});
	CHECK(fu::replaceFile(icon, "icon 8"));
	CHECK_EQ(get(icon), std::string("icon 8"));
	CHECK_EQ(get(bak), std::string("icon 7"));
	CHECK_EQ(fu::pendingCleanup(), std::vector<std::string>{bak});
	// Still in use at the next save: that one goes through on the next name
	// and cleans up after itself.
	CHECK(fu::replaceFile(icon, "icon 9"));
	CHECK_EQ(get(icon), std::string("icon 9"));
	CHECK(fu::exists(bak) && !fu::exists(bak2));
	CHECK_EQ(fu::pendingCleanup(), std::vector<std::string>{bak});
	fu::setReplaceFaultHook(nullptr);
	if (!lockedPath.empty()) {
		CHECK_EQ(fu::retryPendingCleanup(), std::vector<std::string>{bak});
		CHECK(fu::exists(bak));
		lockFile(bak, false);
	}
	// Released (or at the next chance, as before exit): removed.
	CHECK(fu::retryPendingCleanup().empty());
	CHECK(!fu::exists(bak));

	// Interrupted save while an older backup of ours was still in use: the
	// newest backup is the version to restore, the older one is cleaned up.
	bool held = false;
	lockedOnce = false;
	fu::setReplaceFaultHook([&](S s) {
		if (s == S::RemoveBackup && !lockedOnce) {
			lockedOnce = true;
			setMTime(bak, 1000000000);
			held = lockFile(bak, true);
			if (!held) return A::Fail;
		}
		return A::Proceed;
	});
	CHECK(fu::replaceFile(icon, "icon 10"));                // bak = "icon 9", held
	setMTime(icon, 1100000000);
	fu::setReplaceFaultHook([](S s) { return s == S::RenameTemp ? A::Crash : A::Proceed; });
	CHECK(!fu::replaceFile(icon, "icon 11"));               // bak2 = "icon 10"
	fu::setReplaceFaultHook(nullptr);
	CHECK(!fu::exists(icon) && fu::exists(bak2));
	CHECK(fu::recoverReplace(icon));
	CHECK_EQ(get(icon), std::string("icon 10"));
	CHECK_EQ(get(bak), std::string("icon 9"));
	if (held) {
		CHECK_EQ(fu::pendingCleanup(), std::vector<std::string>{bak});
		lockFile(bak, false);
	}
	CHECK(fu::retryPendingCleanup().empty());
	CHECK(!fu::exists(bak) && !fu::exists(bak2) && !fu::exists(tmp));
	CHECK_EQ(get(foreignBak), std::string("user's own copy"));
	CHECK_EQ(get(foreignTmp), std::string("another program's file"));
	CHECK(fu::pendingCleanup().empty());

	testReplaceForeignScratch(dir);
}

static void testReplace()
{
	std::string dir = fu::join(scratch, "replace test");
	fu::makeDirs(dir);
	std::string f = fu::join(dir, "Boxer.prefs");
	std::string tmp = fu::tempPathFor(f), bak = fu::backupPathFor(f);

	CHECK(fu::replaceFile(f, "v1"));
	CHECK_EQ(get(f), std::string("v1"));
	CHECK(fu::replaceFile(f, "v2"));
	CHECK_EQ(get(f), std::string("v2"));
	CHECK(!fu::exists(tmp) && !fu::exists(bak));

	using S = fu::ReplaceStep;
	using A = fu::FaultAction;
	auto failAt = [](S step, A action) {
		fu::setReplaceFaultHook([=](S s) { return s == step ? action : A::Proceed; });
	};

	// A failure at any step keeps the last good version and leaves no debris.
	for (S step : {S::WriteTemp, S::BackupOld, S::RenameTemp}) {
		failAt(step, A::Fail);
		std::string err;
		CHECK(!fu::replaceFile(f, "bad", &err));
		CHECK(!err.empty());
		CHECK_EQ(get(f), std::string("v2"));
		CHECK(!fu::exists(tmp));
		CHECK(!fu::exists(bak));
	}
	// Failing to remove the backup still commits; recovery cleans up.
	failAt(S::RemoveBackup, A::Fail);
	CHECK(fu::replaceFile(f, "v3"));
	CHECK_EQ(get(f), std::string("v3"));
	CHECK(fu::exists(bak));
	CHECK_EQ(fu::pendingCleanup(), std::vector<std::string>{bak});
	fu::setReplaceFaultHook(nullptr);
	CHECK(fu::recoverReplace(f));
	CHECK(!fu::exists(bak));
	CHECK(fu::pendingCleanup().empty());

	// Crash after the old file was moved aside: no final file at all.
	failAt(S::RenameTemp, A::Crash);
	CHECK(!fu::replaceFile(f, "v4"));
	CHECK(!fu::exists(f) && fu::exists(bak) && fu::exists(tmp));
	fu::setReplaceFaultHook(nullptr);
	CHECK(fu::recoverReplace(f));
	CHECK_EQ(get(f), std::string("v3"));
	CHECK(!fu::exists(tmp) && !fu::exists(bak));

	// Crash before backup removal: new version is final, backup dropped.
	failAt(S::RemoveBackup, A::Crash);
	CHECK(!fu::replaceFile(f, "v5"));
	fu::setReplaceFaultHook(nullptr);
	CHECK(fu::recoverReplace(f));
	CHECK_EQ(get(f), std::string("v5"));
	CHECK(!fu::exists(bak));

	// Crash while the temp file is being written: old version intact.
	failAt(S::BackupOld, A::Crash);
	CHECK(!fu::replaceFile(f, "v6"));
	fu::setReplaceFaultHook(nullptr);
	CHECK_EQ(get(f), std::string("v5"));
	// The next replace recovers first and succeeds.
	CHECK(fu::replaceFile(f, "v7"));
	CHECK_EQ(get(f), std::string("v7"));
	CHECK(!fu::exists(tmp) && !fu::exists(bak));

	// First write of a file that does not exist yet, with injected failure.
	std::string g = fu::join(dir, "New State.plist");
	failAt(S::RenameTemp, A::Fail);
	CHECK(!fu::replaceFile(g, "x"));
	CHECK(!fu::exists(g) && !fu::exists(fu::tempPathFor(g)));
	fu::setReplaceFaultHook(nullptr);

	// Real failure: target directory is missing.
	CHECK(!fu::replaceFile(fu::join(dir, "no such dir/x"), "x"));

	testReplaceOwnership(dir);

	// Plist file round trip through replaceFile.
	PlistValue p = PlistValue::dict();
	p.set("k", PlistValue::integer(-42));
	CHECK(writePlistFile(g, p));
	PlistValue q;
	CHECK(readPlistFile(g, q) && q == p);
}

// --- drive names (table from BXDrive.m rules) ---

static void testDriveNames()
{
	struct Row { const char *name; char letter; const char *label; };
	const Row rows[] = {
		{"C.harddisk", 'C', "C"},
		{"C Game.harddisk", 'C', "Game"},
		{"c game.harddrive", 'C', "game"},
		{"D Game CD.cdmedia", 'D', "Game CD"},
		{"D.cdrom", 'D', "D"},
		{"E Disc 2 (2).iso", 'E', "Disc 2"},
		{"A.floppy", 'A', "A"},
		{"A Install Disk.img", 'A', "Install Disk"},
		{"B.ima", 'B', "B"},
		{"B Disk.vfd", 'B', "Disk"},
		{"Y.cdrom", 0, "Y"},           // Y and Z are outside [a-xA-X]
		{"Z Data.harddisk", 0, "Z Data"},
		{"Game Data.harddisk", 0, "Game Data"},
		{"CD.cdrom", 0, "CD"},        // "CD" is not "<L>" or "<L> ..."
		{"D  Two Spaces.cue", 'D', " Two Spaces"},
		{"Windows 3.1.harddisk", 0, "Windows 3.1"},
		{"C (3).harddisk", 'C', "C"}, // suffix stripped, lone letter kept as label
		{"X-COM.cdr", 0, "X-COM"},
		{"Disc (two).iso", 0, "Disc (two)"},
		{"D Music.inst", 'D', "Music"},
		{"README.TXT", 0, "README.TXT"},  // not mountable: extension kept
		{"C.txt", 0, "C.txt"},
		{"My Game.boxer", 0, "My Game"},
	};
	for (const Row &r : rows) {
		CHECK_EQ(preferredDriveLetter(r.name), r.letter);
		CHECK_EQ(preferredVolumeLabel(r.name), std::string(r.label));
	}
	DriveType t;
	CHECK(drivetypes::volumeType("x.cdmedia", t) && t == DriveType::CDROM);
	CHECK(drivetypes::volumeType("x.IMG", t) && t == DriveType::Floppy);
	CHECK(drivetypes::volumeType("x.HardDrive", t) && t == DriveType::HardDisk);
	CHECK(!drivetypes::volumeType("x.gog", t));
	CHECK(!drivetypes::volumeType("x.boxerstate", t));
}

// --- gamebox, configs, mount commands ---

static void testGamebox()
{
	std::string games = fu::join(scratch, "Games Folder");
	std::string box = fu::join(games, "Ultima Underworld (1992).boxer");
	put(fu::join(box, "C Game Drive.harddisk/UW.EXE"), "MZ");
	put(fu::join(box, "D Game CD.cdmedia/tracks.cue"), "FILE");
	put(fu::join(box, "A.floppy/DISK.TXT"), "a");
	put(fu::join(box, "B Save Disk.img"), "img");
	put(fu::join(box, "E.iso"), "iso");
	put(fu::join(box, "D Extra.cdrom/X.TXT"), "x"); // second D: queued
	put(fu::join(box, "Music.cdrom/M.TXT"), "m");   // no letter: CD, keeps with type
	put(fu::join(box, "Notes.txt"), "not a drive");
	put(fu::join(box, ".hidden.harddisk/x"), "hidden");
	put(fu::join(box, "DOSBox Preferences.conf"), "[cpu]\n");
	fu::makeDirs(fu::join(box, "Documentation"));
	put(fu::join(box, "Game Info.plist"),
	    "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
	    "<key>BXLaunchers</key><array>"
	    "<dict><key>BXLauncherTitle</key><string>Play</string><key>BXLauncherPath</key><string>C Game Drive.harddisk/UW.EXE</string>"
	    "<key>BXLauncherArguments</key><string>-fast</string><key>BXLauncherIsDefault</key><true/></dict>"
	    "<dict><key>BXLauncherPath</key><string>C Game Drive.harddisk/SETUP.EXE</string></dict>"
	    "</array><key>CustomKey</key><string>keep me</string></dict></plist>");

	Gamebox g;
	std::string err;
	CHECK(Gamebox::isGameboxPath(box));
	CHECK(g.open(box + "/", &err));
	CHECK_EQ(g.gameName(), std::string("Ultima Underworld (1992)"));
	CHECK(g.hasConfigurationFile());
	CHECK(g.hasDocumentationFolder());
	auto ls = g.launchers();
	CHECK_EQ(ls.size(), (size_t)2);
	if (ls.size() == 2) {
		CHECK_EQ(ls[0].title, std::string("Play"));
		CHECK_EQ(ls[0].arguments, std::string("-fast"));
		CHECK(ls[0].isDefault && !ls[1].isDefault);
		CHECK_EQ(ls[1].title, std::string("SETUP.EXE"));
	}

	// Identifier generated as UUID (type 1) and persisted; unknown key kept.
	CHECK(g.identifier().empty());
	bool persisted = false;
	std::string id = g.ensureIdentifier(&persisted, &err);
	CHECK(persisted);
	CHECK_EQ(id.size(), (size_t)36);
	CHECK(id[14] == '4' && id[8] == '-');
	Gamebox g2;
	CHECK(g2.open(box));
	CHECK_EQ(g2.identifier(), id);
	CHECK(g2.identifierType() == IdentifierType::UUID);
	CHECK_EQ(g2.gameInfo().get("CustomKey")->str(), std::string("keep me"));
	CHECK_EQ(g2.ensureIdentifier(), id);
	CHECK(generateUUID() != generateUUID());

	auto drives = g.bundledDrives();
	std::vector<std::string> summary;
	for (const auto &d : drives)
		summary.push_back(std::string(1, d.letter ? d.letter : '?') + (d.queued ? "q " : " ") + fu::baseName(d.sourcePath) + "|" + d.label);
	// Unlettered Music.cdrom sorts first, finds no CD yet, takes D; the
	// lettered D drives then queue behind it (BXDriveQueue).
	std::vector<std::string> want = {
		"D Music.cdrom|Music",
		"A A.floppy|A",
		"B B Save Disk.img|Save Disk",
		"C C Game Drive.harddisk|Game Drive",
		"Dq D Extra.cdrom|Extra",
		"Dq D Game CD.cdmedia|Game CD",
		"E E.iso|E",
	};
	CHECK_EQ(summary, want);
	for (const auto &d : drives)
		if (fu::baseName(d.sourcePath) == "D Game CD.cdmedia") {
			CHECK_EQ(d.mountPath, fu::join(d.sourcePath, "tracks.cue"));
			CHECK(d.isImage);
		}

	std::vector<std::string> skipped;
	auto cmds = mountCommands(drives, &skipped);
	std::vector<std::string> wantCmds = {
		"MOUNT D \"" + box + "/Music.cdrom/\" -t cdrom -label Music",
		"MOUNT A \"" + box + "/A.floppy/\" -t floppy -label A",
		"IMGMOUNT B \"" + box + "/B Save Disk.img\" -t floppy",
		"MOUNT C \"" + box + "/C Game Drive.harddisk/\" -label \"Game Drive\"",
		"IMGMOUNT E \"" + box + "/E.iso\" -t iso",
	};
	CHECK_EQ(cmds, wantCmds);
	CHECK(skipped.empty());

	// Config list.
	auto confs = sessionConfigFiles("PROGDIR:Configurations", &g);
	CHECK_EQ(confs.size(), (size_t)3);
	if (confs.size() == 3) {
		CHECK_EQ(confs[0].path, std::string("PROGDIR:Configurations/Preflight.conf"));
		CHECK(confs[0].required);
		CHECK_EQ(confs[1].path, g.configurationFilePath());
		CHECK(!confs[1].required);
		CHECK_EQ(confs[2].path, std::string("PROGDIR:Configurations/Launch.conf"));
		CHECK(confs[2].required);
	}
	auto withProfile = sessionConfigFiles("Boxer:Configurations/", nullptr, {"MT-32"});
	CHECK_EQ(withProfile.size(), (size_t)3);
	if (withProfile.size() == 3)
		CHECK_EQ(withProfile[1].path, std::string("Boxer:Configurations/MT-32.conf"));

	// Legacy gamebox: no C drive, root becomes C, legacy default program.
	std::string legacy = fu::join(games, "Old Game.dosbox");
	put(fu::join(legacy, "GAME.EXE"), "MZ");
	put(fu::join(legacy, "Game Info.plist"),
	    "<plist><dict><key>BXDefaultProgramPath</key><string>GAME.EXE</string>"
	    "<key>BXCloseAfterDefaultProgram</key><true/></dict></plist>");
	Gamebox lg;
	CHECK(lg.open(legacy));
	CHECK_EQ(lg.gameName(), std::string("Old Game.dosbox"));
	CHECK(lg.closeAfterDefaultProgram());
	auto ll = lg.launchers();
	CHECK(ll.size() == 1 && ll[0].title == "Launch Old Game.dosbox" && ll[0].path == "GAME.EXE");
	auto ld = lg.bundledDrives();
	CHECK_EQ(ld.size(), (size_t)1);
	if (!ld.empty()) {
		CHECK(ld[0].isGameboxRoot && ld[0].letter == 'C');
		CHECK_EQ(ld[0].shadowName, std::string("C.harddisk"));
		CHECK_EQ(ld[0].label, std::string("Old Game"));
	}
	CHECK_EQ(sessionConfigFiles("Conf:", &lg).size(), (size_t)2);

	CHECK(!g.open(fu::join(games, "missing.boxer"), &err));
	CHECK(!g.open(fu::join(box, "C Game Drive.harddisk"), &err));
}

static void testQuoting()
{
	std::string q;
	CHECK(dosQuote("Work:Games/X.boxer/C.harddisk/", q) && q == "Work:Games/X.boxer/C.harddisk/");
	CHECK(dosQuote("Work:My Games/X Y.boxer/", q) && q == "\"Work:My Games/X Y.boxer/\"");
	CHECK(dosQuote("", q) && q == "\"\"");
	CHECK(dosQuote("a>b", q) && q == "\"a>b\"");
	CHECK(!dosQuote("say \"hi\"", q));
	std::vector<BundledDrive> ds(1);
	ds[0].sourcePath = ds[0].mountPath = "Work:Bad \"Name\".harddisk";
	ds[0].letter = 'C';
	std::vector<std::string> skipped;
	CHECK(mountCommands(ds, &skipped).empty());
	CHECK_EQ(skipped.size(), (size_t)1);

	// The produced line must split into the intended words under DOSBox
	// 0.74's CommandLine rules (misc/setup.cpp), reimplemented here.
	auto split = [](const std::string &line) {
		std::vector<std::string> cmds;
		bool inword = false, inquote = false;
		std::string str;
		for (char c : line) {
			if (inquote) { if (c != '"') str += c; else { inquote = false; cmds.push_back(str); str.clear(); } }
			else if (inword) { if (c != ' ') str += c; else { inword = false; cmds.push_back(str); str.clear(); } }
			else if (c == '"') inquote = true;
			else if (c != ' ') { str += c; inword = true; }
		}
		if (inword || inquote) cmds.push_back(str);
		return cmds;
	};
	ds[0].sourcePath = ds[0].mountPath = "Work:My Games/Ultima (1992).boxer/C Game Drive.harddisk";
	ds[0].label = "Game Drive";
	auto cmds = mountCommands(ds);
	CHECK_EQ(cmds.size(), (size_t)1);
	auto words = split(cmds[0].substr(6)); // after "MOUNT "
	std::vector<std::string> want = {"C", "Work:My Games/Ultima (1992).boxer/C Game Drive.harddisk/", "-label", "Game Drive"};
	CHECK_EQ(words, want);
}

static void testDataLocations()
{
	DataLocations loc;
	std::string games = fu::join(scratch, "Games Folder");
	loc.dataDir = DataLocations::defaultDataDir(games);
	CHECK_EQ(loc.dataDir, games + "/Boxer Data");
	BundledDrive d;
	d.shadowName = "C Game Drive.harddisk";
	CHECK_EQ(loc.shadowRoot("ABC", d), games + "/Boxer Data/Gamebox States/ABC/Current.boxerstate/C Game Drive.harddisk");
	CHECK_EQ(loc.screenshotsDir(), games + "/Boxer Data/Screenshots");
	CHECK_EQ(safeFolderName("net.washboardabs/x:y"), std::string("net.washboardabs_x_y"));

	DataLocations aros;
	aros.dataDir = "Work:Games/Boxer Data";
	CHECK_EQ(aros.gameStateDir("ID"), std::string("Work:Games/Boxer Data/Gamebox States/ID"));
	CHECK_EQ(aros.envarcPrefsPath, std::string("ENVARC:Boxer/Boxer.prefs"));
	CHECK(isExcludedFromScan("Work:Games/Boxer Data", aros));
	CHECK(isExcludedFromScan("work:games/boxer data/Gamebox States/ID", aros));
	CHECK(!isExcludedFromScan("Work:Games/Boxer Database.boxer", aros));
	CHECK(!isExcludedFromScan("Work:Games/Doom.boxer", aros));
	CHECK(isExcludedFromScan("DH1:Other/Boxer Data/x.boxer", aros)); // stale default elsewhere

	std::string msg;
	CHECK(prepareDataDir(loc.dataDir, &msg) == DataDirStatus::Ready);
	CHECK(fu::isDirectory(loc.dataDir));
	CHECK(prepareDataDir("", &msg) == DataDirStatus::Unset);
	std::string ro = fu::join(scratch, "Read Only Games");
	fu::makeDirs(ro);
	chmod(ro.c_str(), 0555);
	if (!fu::isWritableDirectory(ro)) { // skipped when running as root
		CHECK(prepareDataDir(fu::join(ro, "Boxer Data"), &msg) == DataDirStatus::CannotCreate);
		CHECK(prepareDataDir(ro, &msg) == DataDirStatus::NotWritable);
	}
	chmod(ro.c_str(), 0755);

	// Path helpers on AROS-style paths.
	CHECK_EQ(fu::join("Work:", "a"), std::string("Work:a"));
	CHECK_EQ(fu::parent("Work:a"), std::string("Work:"));
	CHECK_EQ(fu::parent("Work:a/b/"), std::string("Work:a"));
	CHECK_EQ(fu::baseName("Work:a/b c.boxer/"), std::string("b c.boxer"));
	CHECK(fu::isWithin("Work:A/b", "work:a"));
	CHECK(!fu::isWithin("Work:Ab", "Work:A"));
	CHECK(fu::isWithin("Work:x", "Work:"));
	CHECK_EQ(fu::relativeTo("Work:A/b/c", "Work:A/"), std::string("b/c"));
}

// --- shadow filesystem ---

static std::vector<std::string> listing(ShadowFileSystem &fs, const std::string &dir)
{
	std::vector<std::string> out;
	void *h = fs.openDir(dir.c_str(), nullptr);
	if (!h) return {"<null>"};
	std::string n;
	bool isDir;
	while (fs.nextEntry(h, n, isDir)) out.push_back(n + (isDir ? "/" : ""));
	fs.closeDir(h);
	std::sort(out.begin(), out.end());
	return out;
}

static std::string readVia(ShadowFileSystem &fs, const std::string &path)
{
	FILE *f = fs.open(path.c_str(), "rb", nullptr);
	if (!f) return "<null>";
	std::string d;
	char buf[256];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
	fclose(f);
	return d;
}

static bool writeVia(ShadowFileSystem &fs, const std::string &path, const char *mode, const std::string &data)
{
	FILE *f = fs.open(path.c_str(), mode, nullptr);
	if (!f) return false;
	fwrite(data.data(), 1, data.size(), f);
	return fclose(f) == 0;
}

static void testShadow()
{
	std::string box = fu::join(scratch, "Shadow Games/Space Quest (1986).boxer");
	std::string c = fu::join(box, "C Game.harddisk");
	put(fu::join(c, "SQ.EXE"), "original exe");
	put(fu::join(c, "SAVE/GAME1.SAV"), "save one");
	put(fu::join(c, "SAVE/GAME2.SAV"), "save two");
	put(fu::join(c, "DATA/LEVEL.DAT"), "level");
	put(fu::join(c, "DATA/SUB/DEEP.DAT"), "deep");
	put(fu::join(c, "CONFIG.CFG"), "cfg=1");
	put(fu::join(box, "D.cdrom/CD.TXT"), "cd");
	std::string shadow = fu::join(scratch, "Shadow Games/Boxer Data/Gamebox States/ID 1/Current.boxerstate/C Game.harddisk");

	std::map<std::string, std::string> before, after;
	snapshot(box, "", before);

	ShadowFileSystem fs;
	fs.addMapping(c + "/", shadow);
	fs.addReadOnlyRoot(fu::join(box, "D.cdrom"));
	auto P = [&](const std::string &rel) { return fu::join(c, rel); };

	// Read original, nothing created.
	CHECK_EQ(readVia(fs, P("SQ.EXE")), std::string("original exe"));
	CHECK(!fu::exists(shadow));
	CHECK(fs.fileExists(P("SQ.EXE").c_str(), nullptr));
	CHECK(fs.dirExists(P("SAVE").c_str(), nullptr));
	CHECK(fs.dirExists(c.c_str(), nullptr));

	// Write a new file: lands in the shadow only.
	CHECK(writeVia(fs, P("SAVE/GAME3.SAV"), "wb+", "save three"));
	CHECK_EQ(get(fu::join(shadow, "SAVE/GAME3.SAV")), std::string("save three"));
	CHECK(!fu::exists(P("SAVE/GAME3.SAV")));
	CHECK_EQ(readVia(fs, P("SAVE/GAME3.SAV")), std::string("save three"));

	// Modify an existing file in place (rb+): copy-on-write keeps the rest.
	{
		FILE *f = fs.open(P("CONFIG.CFG").c_str(), "rb+", nullptr);
		CHECK(f != nullptr);
		if (f) { fseek(f, 4, SEEK_SET); fputc('9', f); fclose(f); }
	}
	CHECK_EQ(readVia(fs, P("CONFIG.CFG")), std::string("cfg=9"));
	CHECK_EQ(get(P("CONFIG.CFG")), std::string("cfg=1"));
	// Append mode also copies first.
	CHECK(writeVia(fs, P("SAVE/GAME1.SAV"), "ab", "+more"));
	CHECK_EQ(readVia(fs, P("SAVE/GAME1.SAV")), std::string("save one+more"));
	// Truncating write does not need the original.
	CHECK(writeVia(fs, P("SAVE/GAME2.SAV"), "wb", "new two"));
	CHECK_EQ(readVia(fs, P("SAVE/GAME2.SAV")), std::string("new two"));
	struct stat st;
	CHECK(fs.stat(P("SAVE/GAME2.SAV").c_str(), nullptr, &st) && st.st_size == 7);
	// Writing into a directory that does not exist fails.
	CHECK(fs.open(P("NODIR/X.TXT").c_str(), "wb", nullptr) == nullptr);
	CHECK(fs.open(P("MISSING.TXT").c_str(), "rb", nullptr) == nullptr);
	CHECK(fs.open(P("MISSING.TXT").c_str(), "rb+", nullptr) == nullptr);

	// Delete an original: hidden, marker in the shadow, original intact.
	CHECK(fs.remove(P("SQ.EXE").c_str(), nullptr));
	CHECK(!fs.fileExists(P("SQ.EXE").c_str(), nullptr));
	CHECK(fs.open(P("SQ.EXE").c_str(), "rb", nullptr) == nullptr);
	CHECK(!fs.stat(P("SQ.EXE").c_str(), nullptr, &st));
	CHECK(fu::isFile(fu::join(shadow, "SQ.EXE.deleted")));
	CHECK_EQ(get(fu::join(shadow, "SQ.EXE.deleted")), std::string(""));
	CHECK(fu::exists(P("SQ.EXE")));
	CHECK(!fs.remove(P("SQ.EXE").c_str(), nullptr)); // already deleted
	CHECK(fs.open(P("SQ.EXE").c_str(), "rb+", nullptr) == nullptr); // no create
	// Delete an original that had a shadow copy: both hidden.
	CHECK(fs.remove(P("CONFIG.CFG").c_str(), nullptr));
	CHECK(!fu::exists(fu::join(shadow, "CONFIG.CFG")));
	CHECK(fu::exists(fu::join(shadow, "CONFIG.CFG.deleted")));
	// Delete a shadow-only file: no marker.
	CHECK(fs.remove(P("SAVE/GAME3.SAV").c_str(), nullptr));
	CHECK(!fu::exists(fu::join(shadow, "SAVE/GAME3.SAV")));
	CHECK(!fu::exists(fu::join(shadow, "SAVE/GAME3.SAV.deleted")));
	CHECK(!fs.remove(P("SAVE/GAME3.SAV").c_str(), nullptr));

	// Recreate after delete: new content, marker gone.
	CHECK(writeVia(fs, P("SQ.EXE"), "wb+", "patched"));
	CHECK_EQ(readVia(fs, P("SQ.EXE")), std::string("patched"));
	CHECK(!fu::exists(fu::join(shadow, "SQ.EXE.deleted")));
	CHECK_EQ(get(P("SQ.EXE")), std::string("original exe"));

	// Enumeration merge: shadow + source, minus deleted, markers hidden.
	std::vector<std::string> want = {"DATA/", "SAVE/", "SQ.EXE"};
	CHECK_EQ(listing(fs, c), want);
	want = {"GAME1.SAV", "GAME2.SAV"};
	CHECK_EQ(listing(fs, P("SAVE")), want);

	// mkdir / rmdir.
	CHECK(fs.makeDir(P("NEWDIR").c_str(), nullptr));
	CHECK(fu::isDirectory(fu::join(shadow, "NEWDIR")));
	CHECK(!fs.makeDir(P("NEWDIR").c_str(), nullptr));
	CHECK(!fs.makeDir(P("DATA").c_str(), nullptr)); // original exists
	CHECK(!fs.makeDir(P("A/B").c_str(), nullptr));  // no parent
	CHECK(writeVia(fs, P("NEWDIR/F.TXT"), "wb", "f"));
	CHECK_EQ(listing(fs, P("NEWDIR")), std::vector<std::string>{"F.TXT"});
	CHECK(fs.removeDir(P("NEWDIR").c_str(), nullptr));
	CHECK(!fs.dirExists(P("NEWDIR").c_str(), nullptr));
	CHECK(!fu::exists(fu::join(shadow, "NEWDIR")));
	CHECK(!fu::exists(fu::join(shadow, "NEWDIR.deleted")));
	CHECK(!fs.removeDir(P("SAVE/GAME1.SAV").c_str(), nullptr)); // not a dir
	CHECK(!fs.removeDir(c.c_str(), nullptr));                     // drive root

	// Remove an original directory (recursive, as the original): marker,
	// and everything below disappears.
	CHECK(fs.removeDir(P("DATA").c_str(), nullptr));
	CHECK(!fs.dirExists(P("DATA").c_str(), nullptr));
	CHECK(!fs.fileExists(P("DATA/LEVEL.DAT").c_str(), nullptr));
	CHECK(!fs.dirExists(P("DATA/SUB").c_str(), nullptr));
	CHECK(fs.open(P("DATA/NEW.DAT").c_str(), "wb", nullptr) == nullptr);
	CHECK(fs.openDir(P("DATA").c_str(), nullptr) == nullptr);
	CHECK(fu::exists(fu::join(shadow, "DATA.deleted")));
	CHECK(fu::exists(P("DATA/LEVEL.DAT")));
	// Recreate the directory: it must be empty.
	CHECK(fs.makeDir(P("DATA").c_str(), nullptr));
	CHECK(fs.dirExists(P("DATA").c_str(), nullptr));
	CHECK_EQ(listing(fs, P("DATA")), std::vector<std::string>{});
	CHECK(!fs.fileExists(P("DATA/LEVEL.DAT").c_str(), nullptr));
	CHECK(writeVia(fs, P("DATA/LEVEL.DAT"), "wb", "new level"));
	CHECK_EQ(listing(fs, P("DATA")), std::vector<std::string>{"LEVEL.DAT"});
	CHECK_EQ(readVia(fs, P("DATA/LEVEL.DAT")), std::string("new level"));

	// Rename across directories: original source hidden, destination shadowed.
	CHECK(fs.move(P("SAVE/GAME1.SAV").c_str(), P("DATA/G1.SAV").c_str(), nullptr));
	CHECK(!fs.fileExists(P("SAVE/GAME1.SAV").c_str(), nullptr));
	CHECK_EQ(readVia(fs, P("DATA/G1.SAV")), std::string("save one+more"));
	CHECK(fu::exists(fu::join(shadow, "SAVE/GAME1.SAV.deleted")));
	CHECK(!fu::exists(fu::join(shadow, "SAVE/GAME1.SAV")));
	// Rename of an unshadowed original file.
	put(P("SAVE/GAME4.SAV"), "four"); // fixture change, before the "before" check below
	snapshot(box, "", before);
	CHECK(fs.move(P("SAVE/GAME4.SAV").c_str(), P("G4.SAV").c_str(), nullptr));
	CHECK_EQ(readVia(fs, P("G4.SAV")), std::string("four"));
	CHECK(!fs.fileExists(P("SAVE/GAME4.SAV").c_str(), nullptr));
	// Move a deleted file fails; move into a missing directory fails.
	CHECK(!fs.move(P("SQ.EXE.nothing").c_str(), P("X").c_str(), nullptr));
	CHECK(!fs.move(P("CONFIG.CFG").c_str(), P("X.CFG").c_str(), nullptr));
	CHECK(!fs.move(P("G4.SAV").c_str(), P("NOPE/G4.SAV").c_str(), nullptr));
	// Directory rename with partly shadowed contents keeps every file.
	CHECK(writeVia(fs, P("SAVE/GAME5.SAV"), "wb", "five"));
	CHECK(fs.move(P("SAVE").c_str(), P("SAVES").c_str(), nullptr));
	want = {"GAME2.SAV", "GAME5.SAV"};
	CHECK_EQ(listing(fs, P("SAVES")), want);
	CHECK_EQ(readVia(fs, P("SAVES/GAME2.SAV")), std::string("new two"));
	CHECK(!fs.dirExists(P("SAVE").c_str(), nullptr));
	// Move a directory onto the name of a deleted original directory: its
	// original children must not show through.
	CHECK(fs.removeDir(P("DATA").c_str(), nullptr));
	CHECK(fs.move(P("SAVES").c_str(), P("DATA").c_str(), nullptr));
	CHECK_EQ(listing(fs, P("DATA")), want);
	// Recreate the source directory name of a moved directory.
	CHECK(fs.makeDir(P("SAVE").c_str(), nullptr));
	CHECK_EQ(listing(fs, P("SAVE")), std::vector<std::string>{});

	// Case-insensitive merge (AROS and macOS filesystems are).
	want = {"DATA/", "G4.SAV", "SAVE/", "SQ.EXE"};
	CHECK_EQ(listing(fs, c), want);

	// Policy and pass-through.
	CHECK(fs.mayWrite(P("X").c_str(), nullptr));
	CHECK(!fs.mayWrite(fu::join(box, "D.cdrom/CD.TXT").c_str(), nullptr));
	CHECK(fs.open(fu::join(box, "D.cdrom/CD.TXT").c_str(), "rb+", nullptr) == nullptr);
	CHECK_EQ(readVia(fs, fu::join(box, "D.cdrom/CD.TXT")), std::string("cd"));
	CHECK(!fs.remove(fu::join(box, "D.cdrom/CD.TXT").c_str(), nullptr));
	std::string outside = fu::join(scratch, "outside dir");
	fu::makeDirs(outside);
	CHECK(writeVia(fs, fu::join(outside, "o.txt"), "wb", "o"));
	CHECK_EQ(get(fu::join(outside, "o.txt")), std::string("o"));
	CHECK(fs.mayWrite(fu::join(outside, "o.txt").c_str(), nullptr));
	CHECK_EQ(listing(fs, outside), std::vector<std::string>{"o.txt"});
	CHECK(fs.remove(fu::join(outside, "o.txt").c_str(), nullptr));
	CHECK(!fu::exists(fu::join(outside, "o.txt")));
	CHECK(!fs.showFile(".DS_Store"));
	CHECK(!fs.showFile("Game Info.plist"));
	CHECK(fs.showFile("GAME.EXE") && fs.showFile(".."));

	// Core acceptance: the gamebox is byte-for-byte and timestamp unchanged.
	snapshot(box, "", after);
	CHECK(before == after);
	if (before != after)
		for (const auto &e : after)
			if (before[e.first] != e.second) fprintf(stderr, "  changed: %s\n", e.first.c_str());
}

// Legacy gamebox: the root is drive C and a CD folder inside it is mapped
// separately (longest root wins).
static void testShadowNested()
{
	std::string box = fu::join(scratch, "Nested Games/Old One.boxer");
	put(fu::join(box, "GAME.EXE"), "g");
	put(fu::join(box, "D.cdrom/CD.DAT"), "cd");
	std::string state = fu::join(scratch, "Nested Games/Boxer Data/Gamebox States/X/Current.boxerstate");
	std::map<std::string, std::string> before, after;
	snapshot(box, "", before);
	ShadowFileSystem fs;
	fs.addMapping(box, fu::join(state, "C.harddisk"));
	fs.addReadOnlyRoot(fu::join(box, "D.cdrom"));
	CHECK(writeVia(fs, fu::join(box, "GAME.EXE"), "rb+", "G"));
	CHECK(fu::exists(fu::join(state, "C.harddisk/GAME.EXE")));
	CHECK(!fs.mayWrite(fu::join(box, "D.cdrom/CD.DAT").c_str(), nullptr));
	CHECK(fs.open(fu::join(box, "D.cdrom/CD.DAT").c_str(), "wb", nullptr) == nullptr);
	snapshot(box, "", after);
	CHECK(before == after);
}

// --- user prefs, one data directory, state found by identifier ---
// --- a data directory that moved with the games folder ---

static void makeGameboxWithId(const std::string &folder, const std::string &name, const std::string &id)
{
	const std::string box = fu::join(folder, name + ".boxer");
	fu::makeDirs(box);
	PlistValue info = PlistValue::dict();
	if (!id.empty()) info.set("BXGameIdentifier", PlistValue::string(id));
	if (!writePlistFile(fu::join(box, "Game Info.plist"), info)) { fprintf(stderr, "cannot write fixture %s\n", box.c_str()); exit(2); }
}

static void makeState(const std::string &dataDir, const std::string &id)
{
	fu::makeDirs(fu::join(fu::join(fu::join(dataDir, "Gamebox States"), safeFolderName(id)), "Current.boxerstate"));
}

static void testMovedDataDir()
{
	const std::string base = fu::join(scratch, "moved data test");
	const std::string oldGames = fu::join(base, "DOS Games");               // no longer exists
	const std::string oldData = fu::join(oldGames, "Boxer Data");
	using M = MovedDataDir;

	// Candidates: same place relative to the old games folder, old name, default name.
	{
		const std::string g = fu::join(base, "Moved");
		auto c = movedDataDirCandidates(fu::join(oldGames, "Saves/Boxer"), oldGames, {g});
		CHECK_EQ(c, (std::vector<std::string>{g + "/Saves/Boxer", g + "/Boxer", g + "/Boxer Data"}));
		c = movedDataDirCandidates(oldData, oldGames, {g, g});
		CHECK_EQ(c, std::vector<std::string>{g + "/Boxer Data"});
		// Old data directory outside the old games folder: no relative guess.
		c = movedDataDirCandidates("DH1:Saves", "Work:DOS Games", {"Work:Games"});
		CHECK_EQ(c, (std::vector<std::string>{"Work:Games/Saves", "Work:Games/Boxer Data"}));
		// The missing directory itself is never a candidate.
		c = movedDataDirCandidates("Work:Games/Boxer Data", "", {"Work:Games"});
		CHECK(c.empty());
	}

	// Match: the moved folder's Boxer Data holds Tyrian's state (and one of a
	// game deleted since).
	const std::string moved = fu::join(base, "DOS Games Moved");
	makeGameboxWithId(moved, "Tyrian", "6F1C-TYRIAN");
	makeGameboxWithId(fu::join(moved, "Shooters"), "Raptor", "RAPTOR:ID");   // a drawer of games
	makeGameboxWithId(moved, "No Id Yet", "");
	makeState(fu::join(moved, "Boxer Data"), "6F1C-TYRIAN");
	makeState(fu::join(moved, "Boxer Data"), "RAPTOR:ID");
	makeState(fu::join(moved, "Boxer Data"), "DELETED-GAME");
	fu::makeDirs(fu::join(moved, "Boxer Data/Screenshots"));
	{
		auto names = gameboxStateNamesIn(moved);
		std::sort(names.begin(), names.end());
		CHECK_EQ(names, (std::vector<std::string>{"6f1c-tyrian", "raptor_id"}));
		std::map<std::string, std::string> before, after;
		snapshot(base, "", before);
		MovedDataDirSearch r = findMovedDataDir(oldData, oldGames, {moved});
		snapshot(base, "", after);
		CHECK(r.result == M::Found);
		CHECK_EQ(r.path, moved + "/Boxer Data");
		CHECK_EQ(r.matchedGames, (size_t)2);
		CHECK(before == after);   // nothing created or changed
		// Same without knowing the old games folder (moved in an earlier session).
		r = findMovedDataDir(oldData, "", {moved});
		CHECK(r.result == M::Found && r.path == moved + "/Boxer Data");
		// Found through the second folder searched (the opened gamebox's drawer).
		r = findMovedDataDir(oldData, oldGames, {fu::join(base, "Elsewhere"), moved});
		CHECK(r.result == M::Found && r.path == moved + "/Boxer Data");
	}

	// No match: a "Boxer Data" with the state of other games only. Same name,
	// different contents: not proposed.
	const std::string other = fu::join(base, "Other Games");
	makeGameboxWithId(other, "Dune", "DUNE-1");
	makeState(fu::join(other, "Boxer Data"), "SOMEONE-ELSES");
	{
		MovedDataDirSearch r = findMovedDataDir(oldData, oldGames, {other});
		CHECK(r.result == M::NotFound);
		CHECK(r.path.empty() && r.matching.empty());
	}
	// No candidate at all; a file of that name is not a directory.
	{
		const std::string none = fu::join(base, "Plain Games");
		makeGameboxWithId(none, "Dune", "DUNE-1");
		CHECK(findMovedDataDir(oldData, oldGames, {none}).result == M::NotFound);
		put(fu::join(none, "Boxer Data"), "not a drawer");
		CHECK(findMovedDataDir(oldData, oldGames, {none}).result == M::NotFound);
		CHECK(findMovedDataDir(oldData, oldGames, {fu::join(base, "Missing Too")}).result == M::NotFound);
	}

	// Uncertain: the name is there but the contents prove nothing.
	{
		const std::string fresh = fu::join(base, "Fresh Games");
		makeGameboxWithId(fresh, "Tyrian", "6F1C-TYRIAN");
		fu::makeDirs(fu::join(fresh, "Boxer Data/Screenshots"));     // no game played yet
		MovedDataDirSearch r = findMovedDataDir(oldData, oldGames, {fresh});
		CHECK(r.result == M::Uncertain);
		CHECK_EQ(r.path, fresh + "/Boxer Data");
		CHECK(r.matching.empty());
		// State there, but no gamebox in the folder to compare it with.
		const std::string bare = fu::join(base, "Bare Folder");
		makeState(fu::join(bare, "Boxer Data"), "6F1C-TYRIAN");
		r = findMovedDataDir(oldData, oldGames, {bare});
		CHECK(r.result == M::Uncertain && r.path == bare + "/Boxer Data");
	}

	// Ambiguous: a custom-named data directory and a default one both hold
	// state of the folder's games.
	{
		const std::string both = fu::join(base, "Both Games");
		makeGameboxWithId(both, "Tyrian", "6F1C-TYRIAN");
		makeState(fu::join(both, "My Saves"), "6F1C-TYRIAN");
		makeState(fu::join(both, "Boxer Data"), "6F1C-TYRIAN");
		MovedDataDirSearch r = findMovedDataDir(fu::join(oldGames, "My Saves"), oldGames, {both});
		CHECK(r.result == M::Ambiguous);
		CHECK(r.path.empty());
		CHECK_EQ(r.matching, (std::vector<std::string>{both + "/My Saves", both + "/Boxer Data"}));
		// With only the custom one holding state, that one is found.
		fu::removeTree(fu::join(both, "Boxer Data/Gamebox States"));
		r = findMovedDataDir(fu::join(oldGames, "My Saves"), oldGames, {both});
		CHECK(r.result == M::Found && r.path == both + "/My Saves");
	}

	// The contents of a gamebox are not searched for games.
	{
		const std::string odd = fu::join(base, "Odd Games");
		makeGameboxWithId(odd, "Tyrian", "6F1C-TYRIAN");
		makeGameboxWithId(fu::join(odd, "Tyrian.boxer"), "Inner", "INNER");
		auto names = gameboxStateNamesIn(odd);
		CHECK_EQ(names, std::vector<std::string>{"6f1c-tyrian"});
	}
}

static void testPrefsAndDataDir()
{
	UserPrefs p;
	std::string err;
	CHECK(parsePrefs("# c\r\n\r\nDataDir = Work:My Games/Boxer Data \r\nFuture=1\n;x\n", p, &err));
	CHECK_EQ(p.dataDir, std::string("Work:My Games/Boxer Data"));
	CHECK_EQ(p.other["Future"], std::string("1"));
	UserPrefs q;
	CHECK(parsePrefs(serializePrefs(p), q));
	CHECK_EQ(q.dataDir, p.dataDir);
	CHECK_EQ(q.other["Future"], std::string("1"));
	UserPrefs bad;
	CHECK(!parsePrefs("DataDir=x\ngarbage\n", bad, &err));
	CHECK(err.find("line 2") != std::string::npos);
	UserPrefs empty;
	CHECK(parsePrefs("", empty) && empty.dataDir.empty());

	// Load order: ENV: copy first, ENVARC: when ENV: has none.
	std::string base = fu::join(scratch, "prefs test");
	DataLocations where;
	where.envPrefsPath = fu::join(base, "ENV/Boxer/Boxer.prefs");
	where.envarcPrefsPath = fu::join(base, "ENVARC/Boxer/Boxer.prefs");
	UserPrefs got;
	CHECK(loadUserPrefs(where, got) == PrefsSource::None);
	UserPrefs a; a.dataDir = "DH1:Data A";
	std::string envErr;
	CHECK(saveUserPrefs(where, a, &err, &envErr)); // creates both Boxer drawers
	CHECK(envErr.empty());
	CHECK(loadUserPrefs(where, got) == PrefsSource::Env && got.dataDir == "DH1:Data A");
	fu::removeTree(fu::parent(where.envPrefsPath)); // ENV: cleared, as after a reboot before copy
	got = UserPrefs();
	CHECK(loadUserPrefs(where, got) == PrefsSource::Envarc && got.dataDir == "DH1:Data A");

	// Replacement of an existing file, then failures keep the last good one.
	UserPrefs b; b.dataDir = "DH1:Data B";
	CHECK(saveUserPrefs(where, b, &err));
	CHECK(get(where.envarcPrefsPath).find("DH1:Data B") != std::string::npos);
	using S = fu::ReplaceStep;
	using A = fu::FaultAction;
	UserPrefs c; c.dataDir = "DH1:Data C";
	for (S step : {S::WriteTemp, S::BackupOld, S::RenameTemp}) {
		fu::setReplaceFaultHook([=](S s) { return s == step ? A::Fail : A::Proceed; });
		CHECK(!saveUserPrefs(where, c, &err));
		fu::setReplaceFaultHook(nullptr);
		got = UserPrefs();
		CHECK(loadUserPrefs(where, got) != PrefsSource::None && got.dataDir == "DH1:Data B");
	}
	// Crash after the old file was moved aside: the next load restores it.
	for (S step : {S::RenameTemp, S::RemoveBackup}) {
		fu::setReplaceFaultHook([=](S s) { return s == step ? A::Crash : A::Proceed; });
		CHECK(!saveUserPrefs(where, c, &err));
		fu::setReplaceFaultHook(nullptr);
		got = UserPrefs();
		// ENV: was not written (the save stopped at ENVARC:), so it still
		// reads B; ENVARC: holds B or C depending on where it stopped.
		CHECK(loadUserPrefs(where, got) == PrefsSource::Env && got.dataDir == "DH1:Data B");
		UserPrefs arc;
		CHECK(parsePrefs(get(where.envarcPrefsPath), arc));
		CHECK_EQ(arc.dataDir, std::string(step == S::RenameTemp ? "DH1:Data B" : "DH1:Data C"));
		CHECK(!fu::exists(fu::backupPathFor(where.envarcPrefsPath)));
		CHECK(!fu::exists(fu::tempPathFor(where.envarcPrefsPath)));
		CHECK(saveUserPrefs(where, b, &err));
	}
	// ENVARC: not writable -> false, last good kept; ENV-only failure is separate.
	std::string arcDir = fu::parent(where.envarcPrefsPath);
	chmod(arcDir.c_str(), 0555);
	if (!fu::isWritableDirectory(arcDir)) {
		CHECK(!saveUserPrefs(where, c, &err));
		CHECK(get(where.envarcPrefsPath).find("DH1:Data B") != std::string::npos);
	}
	chmod(arcDir.c_str(), 0755);
	std::string envDir = fu::parent(where.envPrefsPath);
	chmod(envDir.c_str(), 0555);
	if (!fu::isWritableDirectory(envDir)) {
		envErr.clear();
		CHECK(saveUserPrefs(where, c, &err, &envErr));
		CHECK(!envErr.empty());
		CHECK(get(where.envarcPrefsPath).find("DH1:Data C") != std::string::npos);
	}
	chmod(envDir.c_str(), 0755);

	// Choice precedence. The games folder only feeds the first-run proposal.
	UserPrefs none, conf;
	conf.dataDir = "DH1:Boxer Data";
	auto ch = chooseDataDir("", "", none, "Work:Games");
	CHECK(ch.origin == DataDirOrigin::Proposal && ch.save && ch.mayCreate);
	CHECK_EQ(ch.dataDir, std::string("Work:Games/Boxer Data"));
	CHECK(chooseDataDir("", "", none, "").origin == DataDirOrigin::None);
	ch = chooseDataDir("", "", conf, "Work:Elsewhere");
	CHECK(ch.origin == DataDirOrigin::Configured && !ch.save && !ch.mayCreate);
	CHECK_EQ(ch.dataDir, std::string("DH1:Boxer Data"));
	ch = chooseDataDir("", "RAM:D", conf, "Work:Games");
	CHECK(ch.origin == DataDirOrigin::Chosen && ch.save && ch.dataDir == "RAM:D");
	CHECK(!chooseDataDir("", "DH1:Boxer Data", conf, "").save); // same as configured
	ch = chooseDataDir("RAM:S", "RAM:D", conf, "Work:Games");
	CHECK(ch.origin == DataDirOrigin::Override && !ch.save);

	// A configured directory that vanished is reported, not recreated empty.
	std::string gone = fu::join(scratch, "Vanished Data");
	std::string msg;
	CHECK(prepareDataDir(gone, &msg, false) == DataDirStatus::Missing);
	CHECK(!fu::exists(gone));

	// Games folder (GF:398).
	UserPrefs gp;
	CHECK(parsePrefs("DataDir=DH1:D\ngamesfolder = Work:DOS Games\nX=y\n", gp));
	CHECK_EQ(gp.gamesFolder, std::string("Work:DOS Games"));
	CHECK(gp.other.count("gamesfolder") == 0);
	UserPrefs gq;
	CHECK(parsePrefs(serializePrefs(gp), gq) && gq.gamesFolder == gp.gamesFolder && gq.other["X"] == "y");
	CHECK_EQ(defaultGamesFolder(true), std::string("Work:DOS Games"));
	CHECK_EQ(defaultGamesFolder(false), std::string("SYS:DOS Games"));
	DataLocations gl; gl.dataDir = "Work:DOS Games/Boxer Data";
	CHECK(acceptableGamesFolder("Work:DOS Games", gl));
	CHECK(acceptableGamesFolder("Work:", gl));
	CHECK(!acceptableGamesFolder("", gl));
	CHECK(!acceptableGamesFolder("Work:DOS Games/Boxer Data", gl, &msg));
	CHECK(!acceptableGamesFolder("Work:DOS Games/Boxer Data/Gamebox States", gl));
	CHECK(!acceptableGamesFolder("DH1:Other/Boxer Data", gl));
	CHECK(!acceptableGamesFolder("Work:Games/Dune.boxer", gl, &msg));
	CHECK(msg.find("gamebox") != std::string::npos);
	CHECK(!acceptableGamesFolder("Work:Games/Dune.BOXER/C.harddisk", gl));
	CHECK(acceptableGamesFolder("Work:Games/boxer stuff", gl));

	// Browse your games (GF:778, GF:732): open, or ask to locate a lost folder.
	std::string shelf = fu::join(scratch, "DOS Games");
	CHECK(browseGamesFolder("") == GamesFolderBrowse::NotSet);
	CHECK(browseGamesFolder(shelf) == GamesFolderBrowse::Missing);
	CHECK(!fu::exists(shelf)); // a lost folder is not recreated
	CHECK(fu::makeDirs(shelf));
	CHECK(browseGamesFolder(shelf) == GamesFolderBrowse::Open);
	CHECK(browseGamesFolder(shelf + "/") == GamesFolderBrowse::Open);
	CHECK(::rename(shelf.c_str(), (shelf + " old").c_str()) == 0);
	CHECK(browseGamesFolder(shelf) == GamesFolderBrowse::Missing);
	put(shelf, "not a folder");
	CHECK(browseGamesFolder(shelf) == GamesFolderBrowse::Missing);

	// Move + rename of a gamebox under one configuration: same state path.
	std::string games1 = fu::join(scratch, "My Games"), games2 = fu::join(scratch, "Other Drawer");
	std::string box1 = fu::join(games1, "BoxTest Game.boxer"), box2 = fu::join(games2, "Moved Game.boxer");
	put(fu::join(box1, "C.harddisk/BOXTEST.COM"), "x");
	Gamebox g1;
	CHECK(g1.open(box1));
	bool persisted = false;
	const std::string id = g1.ensureIdentifier(&persisted, &err);
	CHECK(persisted && !id.empty());
	UserPrefs first;
	auto c1 = chooseDataDir("", "", first, games1);
	CHECK(c1.origin == DataDirOrigin::Proposal);
	CHECK(prepareDataDir(c1.dataDir, &msg, c1.mayCreate) == DataDirStatus::Ready);
	first.dataDir = c1.dataDir; // saved after the first run
	DataLocations l1; l1.dataDir = c1.dataDir;
	put(fu::join(l1.currentStatePath(id), "C.harddisk/SAVE.DAT"), "1");
	fu::makeDirs(games2);
	CHECK(::rename(box1.c_str(), box2.c_str()) == 0);
	Gamebox g2;
	CHECK(g2.open(box2));
	CHECK_EQ(g2.ensureIdentifier(), id);
	auto c2 = chooseDataDir("", "", first, games2);
	CHECK(c2.origin == DataDirOrigin::Configured);
	CHECK_EQ(c2.dataDir, c1.dataDir);
	DataLocations l2; l2.dataDir = c2.dataDir;
	CHECK_EQ(get(fu::join(l2.currentStatePath(g2.identifier()), "C.harddisk/SAVE.DAT")), std::string("1"));
	CHECK(!fu::exists(fu::join(games2, "Boxer Data")));
}

static void testImportSource()
{
	std::string games = fu::join(scratch, "Import Games"), data = fu::join(games, "Boxer Data");
	std::string src = fu::join(scratch, "Import Src/Dune");
	put(fu::join(src, "DUNE.EXE"), "MZ");
	put(fu::join(games, "Old.boxer/C.harddisk/X.EXE"), "MZ");
	fu::makeDirs(data);
	DataLocations loc; loc.dataDir = data;
	std::string msg;
	std::map<std::string, std::string> before, after;
	snapshot(scratch, "", before);
	CHECK(checkImportSource(src, loc, games, &msg) == SourceCheck::Ok && msg.empty());
	CHECK(checkImportSource(fu::join(scratch, "nope"), loc, games, &msg) == SourceCheck::Missing);
	CHECK(checkImportSource(fu::join(src, "DUNE.EXE"), loc, games) == SourceCheck::NotFolder);
	CHECK(checkImportSource(fu::join(games, "Old.boxer"), loc, games) == SourceCheck::Gamebox);
	CHECK(checkImportSource(fu::join(games, "Old.boxer/C.harddisk"), loc, games) == SourceCheck::Gamebox);
	CHECK(checkImportSource(data, loc, games) == SourceCheck::BoxerData);
	CHECK(checkImportSource(games, loc, games, &msg) == SourceCheck::GamesFolder);
	CHECK(msg.find("Import Games") != std::string::npos);
	CHECK(checkImportSource(scratch, loc, games) == SourceCheck::GamesFolder);
	DataLocations far; far.dataDir = fu::join(scratch, "Import Src/Elsewhere Data");
	fu::makeDirs(far.dataDir);
	CHECK(checkImportSource(fu::join(scratch, "Import Src"), far, games) == SourceCheck::ContainsDataDir);
	CHECK(checkImportSource(src, loc, "") == SourceCheck::Ok);
	fu::removeTree(far.dataDir);
	snapshot(scratch, "", after);
	CHECK(before == after); // checks never touch the disk

	ImportSession s;
	CHECK(s.chooseSource(games, loc, games) == SourceCheck::GamesFolder);
	CHECK(s.stage == ImportStage::WaitingForSource && s.sourcePath.empty());
	CHECK(s.chooseSource(src, loc, games) == SourceCheck::Ok);
	CHECK(s.stage == ImportStage::LoadingSource && s.sourcePath == src);
	s.cancelSourceSelection();
	CHECK(s.stage == ImportStage::WaitingForSource && s.sourcePath.empty());
	CHECK(fu::isFile(fu::join(src, "DUNE.EXE"))); // cancel leaves the source alone
}

static std::string dosExe() { std::string h(64, '\0'); h[0] = 'M'; h[1] = 'Z'; return h + "code"; }
static std::string winExe()
{
	std::string h(1024, '\0');
	h[0] = 'M'; h[1] = 'Z'; h[24] = 0x40; h[60] = (char)0x80;
	h[0x80] = 'P'; h[0x81] = 'E';
	return h;
}

static void testInstallerScan()
{
	// Policies, verbatim patterns.
	CHECK(isInstallerPath("INSTALL.EXE") && isInstallerPath("sub/Setup.exe") && isInstallerPath("CONFIG.EXE"));
	CHECK(isInstallerPath("DOSINST.COM") && !isInstallerPath("DUNE.EXE") && !isInstallerPath("inst/GAME.EXE"));
	CHECK(isIgnoredImportPath("DirectX/setup.exe") && isIgnoredImportPath("UNIVBE/x.exe"));
	CHECK(isIgnoredImportPath("DOSBox 0.74/") && isIgnoredImportPath("README.EXE") && isIgnoredImportPath("autorun.exe"));
	CHECK(!isIgnoredImportPath("GAME/README") && !isIgnoredImportPath("SETUP.EXE"));
	CHECK(isJunkImportPath("dosbox.conf") && isJunkImportPath("GAME.PIF") && isJunkImportPath("gfw_high.ico"));
	CHECK(!isJunkImportPath("GAME.EXE"));
	CHECK(isPlayableGameTelltale("x/game.ISO") && isPlayableGameTelltale("C.harddisk") && isPlayableGameTelltale("gfw_high.ico"));
	CHECK(!isPlayableGameTelltale("TYRIAN.EXE"));
	CHECK(isInconclusiveDOSProgram("GO.BAT") && !isInconclusiveDOSProgram("GO.EXE"));
	CHECK_EQ(preferredInstaller({"a/SETUP.EXE", "HDINSTAL.EXE", "INSTALL.EXE"}), std::string("INSTALL.EXE"));
	CHECK_EQ(preferredInstaller({"CONFIG.EXE", "XINSTALL.EXE"}), std::string(""));
	std::vector<std::string> d = {"a/b/X", "a/Y", "Z", "W"};
	sortByPathDepth(d);
	CHECK(d == (std::vector<std::string>{"Z", "W", "a/Y", "a/b/X"}));

	FileSystem fs;
	const std::string base = fu::join(scratch, "Installer Scan");
	// Dune-like: INSTALL.EXE preselected over SETUP.EXE, deeper installer last.
	std::string dune = fu::join(base, "Dune");
	put(fu::join(dune, "SETUP.EXE"), dosExe());
	put(fu::join(dune, "INSTALL.EXE"), dosExe());
	put(fu::join(dune, "DATA/CONFIG.EXE"), dosExe());
	put(fu::join(dune, "DUNE.EXE"), dosExe());
	put(fu::join(dune, "README.EXE"), dosExe());       // ignored
	put(fu::join(dune, ".hidden/INSTALL.EXE"), dosExe());
	auto r = scanForInstallers(fs, dune);
	CHECK(r.error == InstallerScanError::None && !r.alreadyInstalled && r.shouldOfferInstallers());
	CHECK(r.installers == (std::vector<std::string>{"INSTALL.EXE", "SETUP.EXE", "DATA/CONFIG.EXE"}));
	CHECK_EQ(r.dosExecutables.size(), size_t(4));
	// Pre-installed: a .conf telltale -> no installer choice (skip).
	std::string pre = fu::join(base, "Pre");
	put(fu::join(pre, "GAME.EXE"), dosExe());
	put(fu::join(pre, "SETUP.EXE"), dosExe());
	put(fu::join(pre, "game.conf"), "[autoexec]\n");
	r = scanForInstallers(fs, pre);
	CHECK(r.alreadyInstalled && !r.shouldOfferInstallers() && r.dosboxConfigurations.size() == 1);
	// No installers: nothing to offer, not an error.
	std::string plain = fu::join(base, "Plain");
	put(fu::join(plain, "TYRIAN.EXE"), dosExe());
	r = scanForInstallers(fs, plain);
	CHECK(r.error == InstallerScanError::None && r.installers.empty() && !r.shouldOfferInstallers());
	// Windows-only (Windows EXE plus a stray batch file).
	std::string win = fu::join(base, "Win");
	put(fu::join(win, "GAME.EXE"), winExe());
	put(fu::join(win, "RUN.BAT"), "@echo off");
	r = scanForInstallers(fs, win);
	CHECK(r.error == InstallerScanError::WindowsOnly);
	CHECK(installerScanErrorText(r.error, "Win").find("Windows game") != std::string::npos);
	// Windows EXE alongside a real DOS program: still DOS.
	put(fu::join(win, "DOSGAME.COM"), "x");
	CHECK(scanForInstallers(fs, win).error == InstallerScanError::None);
	// Nothing executable.
	std::string empty = fu::join(base, "Docs");
	put(fu::join(empty, "MANUAL.TXT"), "x");
	r = scanForInstallers(fs, empty);
	CHECK(r.error == InstallerScanError::NoExecutables);
	CHECK(installerScanErrorText(r.error, "Docs").find("does not contain any MS-DOS programs") != std::string::npos);
	// Ignored DOSBox folder contents are not scanned.
	std::string gog = fu::join(base, "Gog");
	put(fu::join(gog, "DOSBOX/INSTALL.EXE"), dosExe());
	put(fu::join(gog, "GAME.EXE"), dosExe());
	r = scanForInstallers(fs, gog);
	CHECK(r.installers.empty() && r.dosExecutables.size() == 1);

	// Session flow after the scan (IS:169-230).
	DataLocations loc; loc.dataDir = fu::join(scratch, "Data For Scan");
	ImportSession s;
	CHECK(s.chooseSource(dune, loc, "") == SourceCheck::Ok);
	CHECK(s.applyScan(scanForInstallers(fs, dune)) == InstallerScanError::None);
	CHECK(s.stage == ImportStage::WaitingForInstaller && s.scan.installers.front() == "INSTALL.EXE");
	s.skipInstaller();
	CHECK(s.stage == ImportStage::ReadyToFinalize);
	s.cancelSourceSelection();
	CHECK(s.stage == ImportStage::WaitingForSource && s.scan.installers.empty());
	CHECK(s.chooseSource(plain, loc, "") == SourceCheck::Ok);
	CHECK(s.applyScan(scanForInstallers(fs, plain)) == InstallerScanError::None);
	CHECK(s.stage == ImportStage::ReadyToFinalize);          // nothing to install: skipped
	CHECK(s.chooseSource(empty, loc, "") == SourceCheck::Ok);
	CHECK(s.applyScan(scanForInstallers(fs, empty)) == InstallerScanError::NoExecutables);
	CHECK(s.stage == ImportStage::WaitingForSource && s.sourcePath.empty());
}

// The ready panel: the name on its own line, a long one shortened in the
// middle so both ends stay readable.
static void testReadyText()
{
	const std::string longName = "The Long Named BoxTest Adventure Deluxe";
	CHECK_EQ(readyToImportText(longName),
	         "\"" + longName + "\"\nis ready to be imported into your games folder.");
	CHECK_EQ(readyToImportText("Tyrian"), std::string("\"Tyrian\"\nis ready to be imported into your games folder."));
	// Exactly at the limit: unchanged; one over: shortened to the limit.
	const std::string forty(kReadyNameChars, 'a');
	CHECK_EQ(shortenMiddle(forty, kReadyNameChars), forty);
	const std::string longer = "The Secret of Monkey Island Special Edition Part 2 (Floppy)";
	const std::string s = shortenMiddle(longer, kReadyNameChars);
	CHECK(s.size() <= kReadyNameChars);
	CHECK(s.find("...") != std::string::npos);
	CHECK_EQ(s.substr(0, 10), std::string("The Secret"));
	CHECK_EQ(s.substr(s.size() - 8), std::string("(Floppy)"));
	CHECK_EQ(s, std::string("The Secret of ... Part 2 (Floppy)"));
	// Without a word break near the cut: straight through the word.
	CHECK_EQ(shortenMiddle("Abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ", 20), std::string("Abcdefghi...CDEFGHIJ"));
	CHECK(shortenMiddle("Abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ", 20).size() <= 20);
	CHECK_EQ(readyToImportText(longer).substr(0, s.size() + 3), "\"" + s + "\"\n");
	// The startup-program page: same shortening, sentence first.
	CHECK_EQ(startupProgramText("Tyrian"), std::string("Choose the program that starts\n\"Tyrian\":"));
	CHECK_EQ(startupProgramText(longer), "Choose the program that starts\n\"" + s + "\":");
	CHECK_EQ(startupProgramText(longName), "Choose the program that starts\n\"" + longName + "\":");
	// A tiny limit still keeps something of both ends.
	CHECK_EQ(shortenMiddle("abcdefghij", 5), std::string("a...j"));
	CHECK_EQ(shortenMiddle("abcdefghij", 0), std::string("a...j"));
}

static void testGameboxCreation()
{
	// Name cleanup (IP:343-404).
	CHECK_EQ(gameboxNameForSource("ULTIMA8 (1994)(Origin Systems)[Rev.2.12]"), std::string("Ultima 8"));
	CHECK_EQ(gameboxNameForSource("ultima_viii"), std::string("Ultima VIII"));
	CHECK_EQ(gameboxNameForSource("DUNE"), std::string("Dune"));
	CHECK_EQ(gameboxNameForSource("Tyrian.harddisk"), std::string("Tyrian"));
	CHECK_EQ(gameboxNameForSource("  wing--commander  "), std::string("Wing Commander"));
	CHECK_EQ(gameboxNameForSource("(1994)"), std::string("(1994)"));
	CHECK_EQ(validGameboxName("..a/b:c\\d"), std::string("a-b-c-d"));
	CHECK_EQ(incrementedGameboxName("Dune", 1), std::string("Dune.boxer"));
	CHECK_EQ(incrementedGameboxName("Dune", 3), std::string("Dune (3).boxer"));

	// Layout for cancel and failure: <base>/Games is the games folder, the source lies
	// beside it in the games folder's parent, and a gamebox called
	// Dune.boxer already exists with its own files.
	const std::string base = fu::join(scratch, "R1 Base");
	const std::string games = fu::join(base, "Games");
	const std::string src = fu::join(base, "DUNE");
	put(fu::join(src, "INSTALL.EXE"), dosExe());
	put(fu::join(src, "DUNE.DAT"), "data");
	const std::string old = fu::join(games, "Dune.boxer");
	put(fu::join(old, "C.harddisk/SAVE.DAT"), "precious");
	put(fu::join(old, "Game Info.plist"), "<plist version=\"1.0\"><dict/></plist>\n");
	std::map<std::string, std::string> before, after;
	snapshot(base, "", before);

	DataLocations loc; loc.dataDir = fu::join(games, "Boxer Data");
	ImportSession s;
	CHECK(s.chooseSource(src, loc, games) == SourceCheck::Ok);
	std::string err;
	CHECK(s.createGamebox(games, &err));
	CHECK_EQ(s.createdGamebox, fu::join(games, "Dune (2).boxer"));      // never reuses Dune.boxer
	CHECK(fu::isDirectory(s.rootDrivePath()));
	Gamebox made;
	CHECK(made.open(s.createdGamebox) && !made.identifier().empty());
	CHECK(!s.gameDidInstall());
	CHECK(!s.createGamebox(games));                                      // one gamebox per import
	put(fu::join(s.rootDrivePath(), "DUNE/DUNE.EXE"), dosExe());        // "installer" output
	CHECK(s.gameDidInstall());
	CHECK(setDefaultLauncher(made, "C.harddisk/DUNE/DUNE.EXE", &err));
	Gamebox reread;
	CHECK(reread.open(s.createdGamebox));
	auto ls = reread.launchers();
	CHECK(ls.size() == 1 && ls[0].isDefault && ls[0].path == "C.harddisk/DUNE/DUNE.EXE");

	// Cancel: only "Dune (2).boxer" goes; source, existing gamebox and
	// everything else in the parent are byte-identical.
	CHECK(s.discardGamebox(&err));
	CHECK(s.createdGamebox.empty());
	snapshot(base, "", after);
	CHECK(before == after);
	CHECK(s.discardGamebox());                                            // idempotent

	// A tampered record outside the games folder is refused, not deleted.
	ImportSession t;
	t.sourcePath = src;
	t.createdGamebox = src;
	t.gamesFolder = games;
	CHECK(!t.discardGamebox(&err));
	CHECK(fu::isFile(fu::join(src, "INSTALL.EXE")));
	t.createdGamebox = old;   // right place and shape, but not ours
	t.createdIdentifier = "NOT-THE-SAME";
	CHECK(!t.discardGamebox());
	t.createdIdentifier.clear();
	CHECK(!t.discardGamebox());   // not empty either
	t.gamesFolder = fu::join(base, "Elsewhere");
	CHECK(!t.discardGamebox());
	CHECK(fu::isFile(fu::join(old, "C.harddisk/SAVE.DAT")));

	// Source inside the games folder's parent named like the games folder's
	// own gamebox: increments keep going past every existing name.
	put(fu::join(games, "Dune (2).boxer/x"), "other");
	ImportSession u;
	CHECK(u.chooseSource(src, loc, games) == SourceCheck::Ok);
	CHECK(u.createGamebox(games));
	CHECK_EQ(u.createdGamebox, fu::join(games, "Dune (3).boxer"));
	CHECK(u.discardGamebox());
	CHECK(get(fu::join(games, "Dune (2).boxer/x")) == "other");

	// Installer session: source as D, installer started from its folder.
	std::vector<std::string> pre, cmd;
	CHECK(installerSessionCommands("Work:My Src/Dune", "INSTALL.EXE", pre, cmd));
	CHECK(pre == (std::vector<std::string>{"MOUNT D \"Work:My Src/Dune\" -label SOURCE"}));
	CHECK(cmd == (std::vector<std::string>{"D:", "cd \\", "INSTALL.EXE"}));
	pre.clear(); cmd.clear();
	CHECK(installerSessionCommands("Work:Src/Game/", "DISK1/SETUP.EXE", pre, cmd));
	CHECK(cmd[1] == "cd \\DISK1" && cmd[2] == "SETUP.EXE");
	CHECK(!installerSessionCommands("Work:Bad\"Name", "X.EXE", pre, cmd));

	// Missing games folder: nothing created anywhere.
	ImportSession v;
	CHECK(v.chooseSource(src, loc, "") == SourceCheck::Ok);
	CHECK(!v.createGamebox(fu::join(base, "No Such Folder"), &err));
	CHECK(v.createdGamebox.empty() && !fu::exists(fu::join(base, "No Such Folder")));
}

// A gamebox name is taken when "<stem>.boxer", "<stem>" (file or drawer)
// or a lone "<stem>.info" exists; each case moves on to "<stem> (2)" and
// leaves the existing object byte-identical.
static void testGameboxNameCollisions()
{
	const char *cases[] = {"plain file", "drawer", "lone icon"};
	for (int c = 0; c < 3; ++c) {
		const std::string base = fu::join(scratch, std::string("Collide ") + cases[c]);
		const std::string games = fu::join(base, "Games");
		const std::string src = fu::join(base, "DUNE");
		put(fu::join(src, "INSTALL.EXE"), dosExe());
		fu::makeDirs(games);
		if (c == 0) put(fu::join(games, "Dune"), "a plain file");
		if (c == 1) put(fu::join(games, "Dune/inside"), "a drawer's file");
		if (c == 2) put(fu::join(games, "Dune.info"), std::string("\xe3\x10icon", 6));
		std::map<std::string, std::string> before, after;
		snapshot(games, "", before);
		DataLocations loc; loc.dataDir = fu::join(games, "Boxer Data");
		ImportSession s;
		CHECK(s.chooseSource(src, loc, games) == SourceCheck::Ok);
		std::string err;
		CHECK(s.createGamebox(games, &err));
		CHECK_EQ(s.createdGamebox, fu::join(games, "Dune (2).boxer"));
		CHECK_EQ(s.gameName(), std::string("Dune (2)"));
		CHECK(!fu::exists(fu::join(games, "Dune.boxer")));
		CHECK(s.discardGamebox(&err));
		snapshot(games, "", after);
		before.erase("/"); after.erase("/");   // the folder's own mtime
		CHECK(before == after);
	}
}

static void testSourceCopy()
{
	CHECK_EQ(validDOSName("Tyrian 2000"), std::string("tyrian20"));
	CHECK_EQ(validDOSName("My.Game.Files"), std::string("my.game.fil")); // as the original: only the last extension is cut
	CHECK_EQ(validDOSName("Dune"), std::string("dune"));
	CHECK_EQ(validDOSName("!!!"), std::string(""));

	const std::string base = fu::join(scratch, "Copy Base");
	const std::string games = fu::join(base, "Games");
	const std::string src = fu::join(base, "Tyrian 2000");   // in the games folder's parent
	put(fu::join(src, "TYRIAN.EXE"), dosExe());
	put(fu::join(src, "DATA/LEVEL1.DAT"), std::string(200000, 'x'));
	put(fu::join(src, "EMPTY.TXT"), "");
	put(fu::join(src, "TYRIAN.PIF"), "junk");                // IP junk: not copied (a .conf would be a telltale)
	put(fu::join(src, ".hidden"), "h");
	fu::makeDirs(fu::join(src, "SAVES"));
	put(fu::join(games, "Tyrian 2000.boxer/C.harddisk/KEEP.SAV"), "theirs");
	put(fu::join(games, "Tyrian 2000.boxer/Game Info.plist"), "<plist version=\"1.0\"><dict/></plist>\n");
	CHECK(shouldUseSubfolderForSource(src));
	std::map<std::string, std::string> before, after;
	snapshot(base, "", before);

	DataLocations loc; loc.dataDir = fu::join(games, "Boxer Data");
	ImportSession s;
	CHECK(s.chooseSource(src, loc, games) == SourceCheck::Ok);
	CHECK(s.createGamebox(games));
	CHECK_EQ(s.gameName(), std::string("Tyrian 2000 (2)"));
	std::string dest, err;
	CHECK(s.prepareCopyDestination(dest, &err));
	CHECK_EQ(dest, fu::join(s.rootDrivePath(), "tyrian20"));
	SourceCopy c;
	CHECK(c.prepare(src, dest, &err));
	CHECK_EQ(c.fileCount(), size_t(3));
	CHECK(c.totalBytes() == 200000 + dosExe().size());
	int steps = 0;
	while (!c.finished() && steps < 1000) { CHECK(c.step(65536, &err)); ++steps; }
	CHECK(c.finished() && steps > 2 && c.copiedBytes() == c.totalBytes());
	CHECK_EQ(get(fu::join(dest, "DATA/LEVEL1.DAT")).size(), size_t(200000));
	CHECK(fu::isFile(fu::join(dest, "EMPTY.TXT")) && fu::isDirectory(fu::join(dest, "SAVES")));
	CHECK(!fu::exists(fu::join(dest, "TYRIAN.PIF")) && !fu::exists(fu::join(dest, ".hidden")));
	CHECK(s.discardGamebox());
	snapshot(base, "", after);
	CHECK(before == after);

	// Disk full in the middle of a file: readable error naming the file;
	// discarding then removes only the new gamebox.
	ImportSession f;
	CHECK(f.chooseSource(src, loc, games) == SourceCheck::Ok);
	CHECK(f.createGamebox(games));
	CHECK(f.prepareCopyDestination(dest));
	SourceCopy d;
	CHECK(d.prepare(src, dest));
	d.writeFault = [](const std::string &path, uint64_t written) {
		return !(path.find("LEVEL1.DAT") != std::string::npos && written >= 65536);
	};
	bool ok = true;
	while (ok && !d.finished()) ok = d.step(65536, &err);
	CHECK(!ok && err.find("LEVEL1.DAT") != std::string::npos && err.find("disk full") != std::string::npos);
	d.close();
	CHECK(f.discardGamebox());
	snapshot(base, "", after);
	CHECK(before == after);

	// A game installed at its root (telltale present): no subfolder.
	const std::string root = fu::join(base, "Rooted");
	put(fu::join(root, "GAME.EXE"), dosExe());
	put(fu::join(root, "game.conf"), "x");
	CHECK(!shouldUseSubfolderForSource(root));
	put(fu::join(base, "NoExe/README.TXT"), "x");
	CHECK(!shouldUseSubfolderForSource(fu::join(base, "NoExe")));

	std::vector<std::string> ty = {"tyrian/file0001.exe", "tyrian/setup.exe", "tyrian/tyrian.exe"};
	CHECK(preferredStartupProgram(ty, "Tyrian") == 2);
	CHECK(preferredStartupProgram(ty, "Tyrian (2)") == 0);               // no match: current rule
	CHECK(preferredStartupProgram({"DUNE/DUNE.BAT", "DUNE/INSTALL.EXE"}, "Dune") == 0);
	CHECK(preferredStartupProgram({"SETUP.EXE", "GO.EXE"}, "X") == 1);
	CHECK(preferredStartupProgram({"SETUP.EXE"}, "X") == 0);
	CHECK(preferredStartupProgram({}, "X") == -1);
	CHECK(preferredStartupProgram({"a/WINGCOM.EXE", "b/WING COMMANDER.EXE"}, "Wing Commander") == 1);
	auto tt = sidecarToolTypes("ABC-1", "Dune.boxer");
	CHECK(tt.size() == 2 && tt[0] == "BOXERID=ABC-1" && tt[1] == "GAMEBOX=Dune.boxer");
}

int main(int argc, char **argv)
{
	if (argc != 2) { fprintf(stderr, "usage: model_test <scratch dir>\n"); return 2; }
	scratch = argv[1];
	fu::removeTree(scratch);
	if (!fu::makeDirs(scratch)) { fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
	testPlist();
	testReplace();
	testDriveNames();
	testGamebox();
	testQuoting();
	testDataLocations();
	testPrefsAndDataDir();
	testMovedDataDir();
	testShadow();
	testShadowNested();
	testImportSource();
	testInstallerScan();
	testReadyText();
	testGameboxCreation();
	testGameboxNameCollisions();
	testSourceCopy();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
