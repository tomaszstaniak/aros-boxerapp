// Host tests for gamebox cover art and pair renaming (src/model/coverart,
// coverfont, gameboxrename).
// Usage: cover_test <scratch dir> [<TrueType font>]
// Without a font the title-rendering checks are skipped (and said so).
#include "../../src/model/coverart.h"
#include "../../src/model/coverfont.h"
#include "../../src/model/datalocations.h"
#include "../../src/model/fsutil.h"
#include "../../src/model/gamebox.h"
#include "../../src/model/gameboxrename.h"
#include "../../src/model/importsource.h"
#include "../../src/model/plist.h"
#include "../../src/model/sourcecopy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static std::string toStr(int n) { return std::to_string(n); }
static std::string toStr(size_t n) { return std::to_string(n); }
static std::string toStr(uint32_t n) { return std::to_string(n); }
static std::string toStr(ReleaseMedium m) { return std::string("medium:") + mediumName(m); }
static std::string toStr(NameCheck c) { return "check " + std::to_string((int)c); }
static std::string toStr(RenameOutcome::Kind k) { return "outcome " + std::to_string((int)k); }

static std::string scratch, fontPath;

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

static void snapshot(const std::string &root, const std::string &rel, std::map<std::string, std::string> &out)
{
	const std::string p = rel.empty() ? root : fu::join(root, rel);
	if (fu::isDirectory(p)) {
		out[rel + "/"] = "dir";
		std::vector<std::string> names;
		fu::list(p, names);
		for (const auto &n : names) snapshot(root, rel.empty() ? n : rel + "/" + n, out);
	} else {
		out[rel] = get(p);
	}
}

static void setTime(const std::string &path, int year)
{
	struct tm t = {};
	t.tm_year = year - 1900; t.tm_mon = 5; t.tm_mday = 1; t.tm_hour = 12;
	const time_t when = timegm(&t);
	struct timeval tv[2] = {{when, 0}, {when, 0}};
	utimes(path.c_str(), tv);
}

static RGBAImage solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	RGBAImage img(w, h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			uint8_t *p = img.at(x, y);
			p[0] = r; p[1] = g; p[2] = b; p[3] = a;
		}
	return img;
}

// A gamebox with a Game Info.plist carrying identifier id, and (optionally)
// a lone sidecar icon beside it.
static std::string makeGamebox(const std::string &folder, const std::string &name, const std::string &id, bool icon)
{
	const std::string box = fu::join(folder, name + ".boxer");
	put(fu::join(box, "C.harddisk/GAME.EXE"), "MZ game");
	Gamebox g;
	g.open(box);
	g.gameInfo().set("BXGameIdentifier", PlistValue::string(id));
	g.gameInfo().set("BXGameIdentifierType", PlistValue::integer(1));
	g.saveGameInfo();
	if (icon) put(fu::join(folder, name + ".info"), std::string("\xe3\x10icon of ") + name);
	return box;
}

// --- images ---

static void testScale()
{
	RGBAImage img = solid(4, 4, 200, 100, 50);
	RGBAImage half = scaleImage(img, 2, 2);
	CHECK_EQ((int)half.at(1, 1)[0], 200);
	CHECK_EQ((int)half.at(1, 1)[3], 255);
	// Transparent pixels do not darken the colour, they only lower alpha.
	RGBAImage mix(2, 1);
	uint8_t *a = mix.at(0, 0), *b = mix.at(1, 0);
	a[0] = 255; a[3] = 255;
	b[0] = 0; b[3] = 0;
	RGBAImage one = scaleImage(mix, 1, 1);
	CHECK_EQ((int)one.at(0, 0)[0], 255);
	CHECK_EQ((int)one.at(0, 0)[3], 128);
}

// Inflates the stored-block zlib stream encodePNG writes and checks every
// CRC, so the bytes a PNG reader sees are the image's.
static uint32_t crc(const std::string &d)
{
	uint32_t c = 0xFFFFFFFFu;
	for (unsigned char ch : d) {
		c ^= ch;
		for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
	}
	return c ^ 0xFFFFFFFFu;
}
static uint32_t be(const std::string &s, size_t o)
{
	return (uint32_t)(unsigned char)s[o] << 24 | (uint32_t)(unsigned char)s[o + 1] << 16 |
	       (uint32_t)(unsigned char)s[o + 2] << 8 | (unsigned char)s[o + 3];
}

static void testPNG()
{
	RGBAImage img(300, 200);   // more than one 64 KiB stored block
	for (int y = 0; y < img.h; ++y)
		for (int x = 0; x < img.w; ++x) {
			uint8_t *p = img.at(x, y);
			p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = (uint8_t)(x ^ y); p[3] = (uint8_t)(255 - y);
		}
	const std::string png = encodePNG(img);
	CHECK(png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0);
	size_t pos = 8;
	std::string idat;
	bool crcOk = true, sawEnd = false;
	uint32_t w = 0, h = 0;
	while (pos + 12 <= png.size()) {
		const uint32_t n = be(png, pos);
		const std::string type = png.substr(pos + 4, 4);
		const std::string body = png.substr(pos + 8, n);
		if (crc(type + body) != be(png, pos + 8 + n)) crcOk = false;
		if (type == "IHDR") { w = be(body, 0); h = be(body, 4); CHECK(body[8] == 8 && body[9] == 6); }
		if (type == "IDAT") idat += body;
		if (type == "IEND") sawEnd = true;
		pos += 12 + n;
	}
	CHECK(crcOk);
	CHECK(sawEnd);
	CHECK_EQ((int)w, 300);
	CHECK_EQ((int)h, 200);
	std::string raw;
	size_t p = 2;
	bool last = false;
	while (!last && p + 5 <= idat.size()) {
		last = idat[p] & 1;
		const size_t len = (unsigned char)idat[p + 1] | (unsigned char)idat[p + 2] << 8;
		const size_t nlen = (unsigned char)idat[p + 3] | (unsigned char)idat[p + 4] << 8;
		CHECK_EQ(len, (~nlen) & 0xFFFF);
		raw.append(idat, p + 5, len);
		p += 5 + len;
	}
	uint32_t a = 1, b = 0;
	for (unsigned char ch : raw) { a = (a + ch) % 65521; b = (b + a) % 65521; }
	CHECK_EQ(be(idat, p), (b << 16) | a);
	CHECK_EQ(raw.size(), (size_t)200 * (300 * 4 + 1));
	bool same = true;
	for (int y = 0; y < 200 && same; ++y) {
		same = raw[(size_t)y * 1201] == 0 &&
		       std::memcmp(&raw[(size_t)y * 1201 + 1], img.at(0, y), 1200) == 0;
	}
	CHECK(same);
}

// --- medium (BXGameProfile.m:77-125) ---

static void testMedium()
{
	const std::string base = fu::join(scratch, "Medium");
	auto game = [&](const std::string &name, int year) {
		const std::string dir = fu::join(base, name);
		put(fu::join(dir, "GAME.EXE"), "MZ");
		put(fu::join(dir, "DATA/LEVEL1.DAT"), "data");
		setTime(fu::join(dir, "DATA/LEVEL1.DAT"), year);
		setTime(fu::join(dir, "DATA"), year);
		setTime(fu::join(dir, "GAME.EXE"), year);
		return dir;
	};
	CHECK_EQ(mediumOfGameAt(game("Old", 1985)), ReleaseMedium::Diskette525);
	CHECK_EQ(mediumOfGameAt(game("Mid", 1991)), ReleaseMedium::Diskette35);
	CHECK_EQ(mediumOfGameAt(game("New", 1996)), ReleaseMedium::Diskette35);   // small: the default
	// A date before 1981 is not trusted (BXInvalidGameDateThreshold).
	CHECK_EQ(mediumOfGameAt(game("Epoch", 1975)), ReleaseMedium::Diskette35);
	// More than 30 MiB of files: a CD game.
	const std::string big = game("Big", 1996);
	const std::string data = fu::join(big, "CD.DAT");
	put(data, "x");
	CHECK(truncate(data.c_str(), 31LL * 1024 * 1024) == 0);
	setTime(data, 1996);
	CHECK_EQ(mediumOfGameAt(big), ReleaseMedium::CDROM);
	// Hidden entries are skipped.
	const std::string hidden = game("Hidden", 1996);
	put(fu::join(hidden, ".old"), "x");
	setTime(fu::join(hidden, ".old"), 1984);
	CHECK_EQ(mediumOfGameAt(hidden), ReleaseMedium::Diskette35);
	CHECK_EQ(mediumOfGameAt(fu::join(base, "No Such Game")), ReleaseMedium::Diskette35);
}

// --- bootleg templates (BXBootlegCoverArt.m) ---

static void testBootlegStyles()
{
	BootlegStyle j = bootlegStyle(ReleaseMedium::CDROM);
	CHECK(j.regionX == 22 && j.regionY == 36 && j.regionW == 92 && j.regionH == 60);
	CHECK(j.fontSize == 14 && j.lineHeight == 20);
	BootlegStyle d35 = bootlegStyle(ReleaseMedium::Diskette35);
	CHECK(d35.regionX == 24 && d35.regionY == 16 && d35.regionW == 80 && d35.regionH == 56);
	CHECK(d35.fontSize == 14 && d35.lineHeight == 18);
	BootlegStyle d525 = bootlegStyle(ReleaseMedium::Diskette525);
	CHECK(d525.regionX == 16 && d525.regionY == 6 && d525.regionW == 96 && d525.regionH == 32);
	CHECK(d525.fontSize == 12 && d525.lineHeight == 16);
	// Unknown media get the 3.5" template, as +bootlegCoverArtForGamebox:.
	CHECK(bootlegStyle(ReleaseMedium::Unknown).medium == ReleaseMedium::Diskette35);
}

static void testTitles()
{
	CoverFont font;
	if (fontPath.empty() || !font.loadFile(fontPath)) {
		printf("title checks skipped: no font (pass a TrueType file as the 2nd argument)\n");
		return;
	}
	auto lines = wrapTitle(font, "The Secret of Monkey Island", 14, 92);
	CHECK(lines.size() >= 2);
	for (const auto &l : lines) CHECK(font.width(l, 14) <= 92);
	std::string joined;
	for (const auto &l : lines) joined += (joined.empty() ? "" : " ") + l;
	CHECK_EQ(joined, std::string("The Secret of Monkey Island"));
	// A word wider than the box is broken between characters.
	auto broken = wrapTitle(font, "Supercalifragilisticexpialidocious", 14, 80);
	CHECK(broken.size() >= 2);
	for (const auto &l : broken) CHECK(font.width(l, 14) <= 80);
	CHECK(wrapTitle(font, "   ", 14, 80).empty());
	CHECK_EQ(wrapTitle(font, "Tyrian", 14, 92).size(), (size_t)1);

	// Ink stays inside the template's box (a pixel of anti-aliasing aside),
	// and lines beyond the box are left out: 5.25" holds two lines.
	for (ReleaseMedium m : {ReleaseMedium::CDROM, ReleaseMedium::Diskette35, ReleaseMedium::Diskette525}) {
		const BootlegStyle st = bootlegStyle(m);
		BootlegArt none;
		RGBAImage img = renderBootleg(m, "One Two Three Four Five Six Seven Eight Nine Ten", none, font);
		CHECK_EQ(img.w, 128);
		int ink = 0, outside = 0, minY = 128, maxY = -1;
		for (int y = 0; y < 128; ++y)
			for (int x = 0; x < 128; ++x) {
				if (img.at(x, y)[3] == 0) continue;
				++ink;
				minY = std::min(minY, y); maxY = std::max(maxY, y);
				if (x < st.regionX - 2 || x > st.regionX + st.regionW + 2 ||
				    y < st.regionY - 4 || y > st.regionY + st.regionH + 1) ++outside;
			}
		CHECK(ink > 50);
		CHECK_EQ(outside, 0);
		// The text colour (0, 0.1, 0.2) at 90% where the ink is solid.
		int solidInk = 0;
		for (int y = 0; y < 128; ++y)
			for (int x = 0; x < 128; ++x) {
				const uint8_t *p = img.at(x, y);
				if (p[3] >= 229) { ++solidInk; CHECK(p[0] == 0 && p[1] == 26 && p[2] == 51); }
			}
		CHECK(solidInk > 0);
		CHECK(maxY - minY < st.regionH + 4);
	}
	// Base and top layers: the title sits between them.
	RGBAImage base = solid(128, 128, 255, 255, 255), glass = solid(128, 128, 255, 0, 0, 64);
	BootlegArt art{&base, &glass};
	RGBAImage withArt = renderBootleg(ReleaseMedium::CDROM, "Tyrian", art, font);
	CHECK_EQ((int)withArt.at(0, 0)[3], 255);
	CHECK(withArt.at(0, 0)[0] == 255 && withArt.at(0, 0)[1] < 255);   // glass over the base
}

// --- BXCoverArt ---

static void testCoverArt()
{
	// 200x100 opaque picture: fitted to 120x60 (128 - 2 x blur 4), centred,
	// standing 5 px above the bottom (blur 4 + offset 1).
	RGBAImage pic = solid(200, 100, 200, 0, 0);
	RGBAImage shine = solid(128, 128, 255, 255, 255, 255);
	RGBAImage icon = renderCoverArt(pic, shine, 128);
	CHECK_EQ(icon.w, 128);
	CHECK_EQ(icon.h, 128);
	const int left = 4, right = 124, top = 128 - 65, bottom = 123;   // rows [63, 123)
	CHECK_EQ((int)icon.at(64, 90)[3], 255);
	// Shine at 25% over red: 200 + (255 - 200) / 4, green 0 -> 64.
	CHECK(std::abs((int)icon.at(64, 90)[0] - 214) <= 2);
	CHECK(std::abs((int)icon.at(64, 90)[1] - 64) <= 2);
	CHECK_EQ((int)icon.at(2, 2)[3], 0);                   // outside: transparent
	CHECK_EQ((int)icon.at(64, top - 10)[3], 0);           // above the frame
	CHECK((int)icon.at(64, bottom + 2)[3] > 0);           // shadow below
	CHECK((int)icon.at(64, bottom + 2)[0] < 40);          // and it is dark
	CHECK((int)icon.at(left - 1, 90)[3] > 0);             // outline / shadow at the edge
	CHECK((int)icon.at(right, 90)[3] > 0);
	// Inner glow: the frame's edge is lighter than its middle.
	CHECK(icon.at(left, 90)[1] > icon.at(64, 90)[1]);
	// A tall picture keeps its proportions: 50x100 -> 60x120 box.
	RGBAImage tall = renderCoverArt(solid(50, 100, 0, 0, 200), RGBAImage(), 128);
	CHECK_EQ((int)tall.at(64, 64)[3], 255);
	CHECK_EQ((int)tall.at(20, 64)[3], 0);
	// A picture with transparency is used as it is, only fitted.
	RGBAImage clear = solid(64, 64, 0, 255, 0, 255);
	clear.at(0, 0)[3] = 0;
	CHECK(imageHasTransparency(clear));
	CHECK(!imageHasTransparency(pic));
	RGBAImage asIs = renderCoverArt(clear, shine, 128);
	CHECK_EQ((int)asIs.at(64, 64)[1], 255);              // no shine, no glow
	CHECK_EQ((int)asIs.at(64, 64)[0], 0);
	CHECK(renderCoverArt(RGBAImage(), shine).empty());
}

// --- what the gamebox keeps, so the cover can be rendered again ---

static void testCoverChoice()
{
	const std::string games = fu::join(scratch, "Choice Games");
	const std::string boxPath = makeGamebox(games, "Dune", "ID-DUNE", false);
	Gamebox box;
	CHECK(box.open(boxPath));
	CoverChoice none = readCoverChoice(box);
	CHECK(none.style == CoverStyle::Automatic && none.isBootleg());
	CHECK_EQ(none.bootlegMedium(), ReleaseMedium::Diskette35);

	CoverChoice c;
	c.style = CoverStyle::Automatic;
	c.detected = ReleaseMedium::CDROM;
	setCoverChoice(box, c);
	CHECK(box.saveGameInfo());
	Gamebox again;
	CHECK(again.open(boxPath));
	CoverChoice r = readCoverChoice(again);
	CHECK(r.style == CoverStyle::Automatic);
	CHECK_EQ(r.bootlegMedium(), ReleaseMedium::CDROM);
	c.style = CoverStyle::Diskette525;
	setCoverChoice(box, c);
	CHECK_EQ(readCoverChoice(box).bootlegMedium(), ReleaseMedium::Diskette525);
	// Other keys are kept.
	CHECK(again.identifier() == "ID-DUNE");

	CHECK_EQ(coverPictureName("Work:Pics/Dune Box.PNG"), std::string("Cover Art.png"));
	CHECK_EQ(coverPictureName("RAM:scan"), std::string("Cover Art"));

	// Storing a picture: copied in, recorded; the next one takes the other
	// name and the first goes only after the record names the new one.
	const std::string pic1 = fu::join(scratch, "Pictures/front.png");
	const std::string pic2 = fu::join(scratch, "Pictures/back.png");
	put(pic1, "PNG one");
	put(pic2, "PNG two");
	std::string err;
	CHECK(storeCoverPicture(box, pic1, &err));
	CHECK_EQ(get(fu::join(boxPath, "Cover Art.png")), std::string("PNG one"));
	Gamebox reread;
	reread.open(boxPath);
	CoverChoice p = readCoverChoice(reread);
	CHECK(p.style == CoverStyle::Picture);
	CHECK_EQ(p.picture, std::string("Cover Art.png"));
	CHECK_EQ(p.detected, ReleaseMedium::CDROM);           // the detected medium stays
	CHECK(storeCoverPicture(box, pic2, &err));
	CHECK_EQ(get(fu::join(boxPath, "Cover Art (2).png")), std::string("PNG two"));
	CHECK(!fu::exists(fu::join(boxPath, "Cover Art.png")));
	reread.open(boxPath);
	CHECK_EQ(readCoverChoice(reread).picture, std::string("Cover Art (2).png"));

	// The Game Info.plist write fails: the record and the old picture stay,
	// the new copy is removed.
	int calls = 0;
	fu::setReplaceFaultHook([&](fu::ReplaceStep s) {
		if (s == fu::ReplaceStep::WriteTemp && ++calls == 2) return fu::FaultAction::Fail;
		return fu::FaultAction::Proceed;
	});
	CHECK(!storeCoverPicture(box, pic1, &err));
	fu::setReplaceFaultHook(nullptr);
	reread.open(boxPath);
	CHECK_EQ(readCoverChoice(reread).picture, std::string("Cover Art (2).png"));
	CHECK_EQ(get(fu::join(boxPath, "Cover Art (2).png")), std::string("PNG two"));
	CHECK(!fu::exists(fu::join(boxPath, "Cover Art.png")));
	CHECK(!storeCoverPicture(box, fu::join(scratch, "Pictures/missing.png"), &err));

	// A record whose picture name has a path in it is not followed.
	PlistValue d = PlistValue::dict();
	d.set("Style", PlistValue::string("picture"));
	d.set("Picture", PlistValue::string("../elsewhere.png"));
	box.gameInfo().set("AROSCoverArt", d);
	CHECK(readCoverChoice(box).style == CoverStyle::Automatic);
}

// --- rename (IS:457-571), roll-back, finding a gamebox from its icon ---

static int iconUpdates;
static std::string lastIconStem;
static bool updateIconOk(const std::string &stem, std::string *)
{
	++iconUpdates;
	lastIconStem = stem;
	return true;
}

static void testRenameChecks()
{
	const std::string games = fu::join(scratch, "Check Games");
	const std::string box = makeGamebox(games, "Dune", "ID-1", true);
	std::string s, msg;
	CHECK_EQ(checkGameboxRename(box, "Dune II", s, &msg), NameCheck::Ok);
	CHECK_EQ(s, std::string("Dune II"));
	CHECK_EQ(checkGameboxRename(box, "Dune", s, &msg), NameCheck::Unchanged);
	CHECK_EQ(checkGameboxRename(box, "...", s, &msg), NameCheck::Empty);
	CHECK_EQ(checkGameboxRename(box, "a/b:c", s, &msg), NameCheck::Ok);
	CHECK_EQ(s, std::string("a-b-c"));
	CHECK_EQ(checkGameboxRename(box, std::string(101, 'x'), s, &msg), NameCheck::TooLong);
	CHECK_EQ(checkGameboxRename(box, "DUNE", s, &msg), NameCheck::Ok);     // case only: its own objects
	// Each of the three objects of a name makes it taken (rules of 2d96121).
	put(fu::join(games, "Taken1.boxer/x"), "x");
	put(fu::join(games, "Taken2"), "plain file");
	put(fu::join(games, "Taken3/inside"), "drawer");
	put(fu::join(games, "Taken4.info"), "lone icon");
	for (const char *n : {"Taken1", "Taken2", "Taken3", "Taken4"}) {
		CHECK_EQ(checkGameboxRename(box, n, s, &msg), NameCheck::Taken);
		CHECK(msg.find("already taken") != std::string::npos);
	}
	// Case-only change while a plain object of the new spelling's name exists.
	const std::string box2 = makeGamebox(games, "Keen", "ID-2", true);
	put(fu::join(games, "Keen"), "a plain file");
	CHECK_EQ(checkGameboxRename(box2, "KEEN", s, &msg), NameCheck::Taken);
}

static void testRenamePair()
{
	const std::string games = fu::join(scratch, "Rename Games");
	DataLocations loc;
	loc.dataDir = fu::join(scratch, "Rename Data");
	std::string box = makeGamebox(games, "Dune", "ID-DUNE", true);
	// A save in the data directory, keyed by the identifier.
	const std::string save = fu::join(loc.currentStatePath("ID-DUNE"), "C.harddisk/DUNE/SAVE1.SAV");
	put(save, "a saved game");

	iconUpdates = 0;
	RenameOutcome o = renameGameboxPair(box, "Dune II", updateIconOk);
	CHECK_EQ(o.kind, RenameOutcome::Renamed);
	CHECK(o.message.empty());
	CHECK_EQ(o.gameboxPath, fu::join(games, "Dune II.boxer"));
	CHECK_EQ(o.iconStem, fu::join(games, "Dune II"));
	CHECK(fu::isDirectory(fu::join(games, "Dune II.boxer")));
	CHECK_EQ(get(fu::join(games, "Dune II.info")), std::string("\xe3\x10icon of Dune"));
	CHECK(!fu::exists(fu::join(games, "Dune.boxer")) && !fu::exists(fu::join(games, "Dune.info")));
	CHECK_EQ(iconUpdates, 1);
	CHECK_EQ(lastIconStem, fu::join(games, "Dune II"));
	CHECK(o.iconUpdated);
	// The renamed gamebox has the same identifier, so the same save.
	Gamebox g;
	CHECK(g.open(o.gameboxPath));
	CHECK_EQ(g.identifier(), std::string("ID-DUNE"));
	CHECK_EQ(get(fu::join(loc.currentStatePath(g.identifier()), "C.harddisk/DUNE/SAVE1.SAV")),
	         std::string("a saved game"));

	// Case-only rename goes through a temporary name.
	o = renameGameboxPair(o.gameboxPath, "DUNE II", updateIconOk);
	CHECK_EQ(o.kind, RenameOutcome::Renamed);
	std::vector<std::string> names;
	fu::list(games, names);
	bool upper = false;
	for (const auto &n : names) if (n == "DUNE II.boxer") upper = true;
	CHECK(upper);
	CHECK(fu::exists(fu::join(games, "DUNE II.info")));
	box = o.gameboxPath;

	// Refused names change nothing.
	put(fu::join(games, "Busy.info"), "someone else's icon");
	std::map<std::string, std::string> before, after;
	snapshot(games, "", before);
	o = renameGameboxPair(box, "Busy", updateIconOk);
	CHECK_EQ(o.kind, RenameOutcome::NotRenamed);
	CHECK(o.message.find("already taken") != std::string::npos);
	snapshot(games, "", after);
	CHECK(before == after);

	// Failure injected between the two renames -> rolled back.
	snapshot(games, "", before);
	setRenameFaultHook([](RenameStep s) { return s == RenameStep::MoveIcon; });
	o = renameGameboxPair(box, "Arrakis", updateIconOk);
	setRenameFaultHook(nullptr);
	CHECK_EQ(o.kind, RenameOutcome::RolledBack);
	CHECK_EQ(o.gameboxPath, box);
	CHECK(o.message.find("Nothing was changed") != std::string::npos);
	after.clear();
	snapshot(games, "", after);
	CHECK(before == after);

	// The first step fails: nothing changed.
	setRenameFaultHook([](RenameStep s) { return s == RenameStep::MoveGamebox; });
	o = renameGameboxPair(box, "Arrakis", updateIconOk);
	setRenameFaultHook(nullptr);
	CHECK_EQ(o.kind, RenameOutcome::NotRenamed);
	after.clear();
	snapshot(games, "", after);
	CHECK(before == after);

	// Icon move and the roll-back both fail -> reported, recoverable:
	// the old icon finds the renamed gamebox by its identifier.
	setRenameFaultHook([](RenameStep s) { return s == RenameStep::MoveIcon || s == RenameStep::RestoreGamebox; });
	iconUpdates = 0;
	o = renameGameboxPair(box, "Arrakis", updateIconOk);
	setRenameFaultHook(nullptr);
	CHECK_EQ(o.kind, RenameOutcome::IconLeftBehind);
	CHECK(o.message.find("still has the old name") != std::string::npos);
	CHECK_EQ(iconUpdates, 0);
	CHECK(fu::isDirectory(fu::join(games, "Arrakis.boxer")));
	CHECK(fu::exists(fu::join(games, "DUNE II.info")));
	CHECK(!fu::exists(fu::join(games, "DUNE II.boxer")));
	auto cands = locateSidecarGamebox(games, "DUNE II.boxer", "ID-DUNE");
	CHECK_EQ(cands.size(), (size_t)1);
	CHECK(sidecarLookupIsCertain(cands));
	CHECK_EQ(cands[0].path, fu::join(games, "Arrakis.boxer"));
	std::string err;
	CHECK(repairSidecarName(fu::join(games, "DUNE II"), cands[0].path, updateIconOk, &err));
	CHECK(fu::exists(fu::join(games, "Arrakis.info")) && !fu::exists(fu::join(games, "DUNE II.info")));
	CHECK_EQ(lastIconStem, fu::join(games, "Arrakis"));
	box = cands[0].path;

	// The icon update fails: the pair is renamed, the failure reported.
	setRenameFaultHook([](RenameStep s) { return s == RenameStep::UpdateIcon; });
	o = renameGameboxPair(box, "Dune", updateIconOk);
	setRenameFaultHook(nullptr);
	CHECK_EQ(o.kind, RenameOutcome::Renamed);
	CHECK(!o.iconUpdated);
	CHECK(o.message.find("icon could not be updated") != std::string::npos);
	CHECK(fu::exists(fu::join(games, "Dune.info")) && fu::isDirectory(fu::join(games, "Dune.boxer")));

	// A gamebox without an icon is renamed alone; no icon is made up.
	const std::string bare = makeGamebox(games, "Bare", "ID-BARE", false);
	iconUpdates = 0;
	o = renameGameboxPair(bare, "Bare Game", updateIconOk);
	CHECK_EQ(o.kind, RenameOutcome::Renamed);
	CHECK(o.iconStem.empty());
	CHECK_EQ(iconUpdates, 0);
	CHECK(!fu::exists(fu::join(games, "Bare Game.info")));

	// Repair never replaces an icon that exists.
	put(fu::join(games, "Stale.info"), "stale");
	CHECK(!repairSidecarName(fu::join(games, "Stale"), fu::join(games, "Dune.boxer"), updateIconOk, &err));
	CHECK_EQ(get(fu::join(games, "Dune.info")), std::string("\xe3\x10icon of Dune"));
	CHECK(fu::exists(fu::join(games, "Stale.info")));

	// The save is where it was all along.
	CHECK_EQ(get(save), std::string("a saved game"));
}

static void testSidecarLookup()
{
	const std::string games = fu::join(scratch, "Lookup Games");
	makeGamebox(games, "Tyrian", "ID-T", false);
	makeGamebox(games, "Tyrian Copy", "ID-T", false);     // an identifier conflict
	makeGamebox(games, "Keen", "ID-K", false);
	// Several gameboxes with the icon's identifier: ask.
	auto c = locateSidecarGamebox(games, "", "ID-T");
	CHECK_EQ(c.size(), (size_t)2);
	CHECK(!sidecarLookupIsCertain(c));
	// GAMEBOX names one whose identifier is another: ask.
	c = locateSidecarGamebox(games, "Keen.boxer", "ID-X");
	CHECK_EQ(c.size(), (size_t)1);
	CHECK(c[0].namedByIcon && !c[0].identifierMatches);
	CHECK(!sidecarLookupIsCertain(c));
	// GAMEBOX and identifier agree: certain.
	c = locateSidecarGamebox(games, "Keen.boxer", "ID-K");
	CHECK_EQ(c.size(), (size_t)1);
	CHECK(c[0].namedByIcon && c[0].identifierMatches);
	CHECK(sidecarLookupIsCertain(c));
	// GAMEBOX with a path is not followed; nothing found -> empty.
	CHECK(locateSidecarGamebox(games, "../Keen.boxer", "").empty());
	CHECK(locateSidecarGamebox(games, "Gone.boxer", "ID-NONE").empty());
	CHECK_EQ(gameboxesWithIdentifier(games, "ID-K").size(), (size_t)1);
	CHECK(gameboxesWithIdentifier(games, "").empty());
}

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) { fprintf(stderr, "usage: cover_test <scratch dir> [<font.ttf>]\n"); return 2; }
	scratch = argv[1];
	if (argc == 3) fontPath = argv[2];
	fu::removeTree(scratch);
	if (!fu::makeDirs(scratch)) { fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
	testScale();
	testPNG();
	testMedium();
	testBootlegStyles();
	testTitles();
	testCoverArt();
	testCoverChoice();
	testRenameChecks();
	testRenamePair();
	testSidecarLookup();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
