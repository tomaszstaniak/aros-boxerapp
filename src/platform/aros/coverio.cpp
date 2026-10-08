// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "coverio.h"
#include "../../model/fsutil.h"
#include "../../model/sourcecopy.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/workbench.h>
#include <proto/datatypes.h>
#include <proto/intuition.h>
#include <datatypes/pictureclass.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <clib/alib_protos.h>

#include <cstdio>
#include <cstring>

struct Library *DataTypesBase;

namespace boxer {

namespace {

// Opens a library into one of the global bases for the length of a call,
// as main.cpp does: BoxerUI keeps no library open that it does not use.
struct LibScope {
	struct Library **base, *saved, *lib;
	LibScope(struct Library **b, const char *name, ULONG version) : base(b), saved(*b)
	{
		lib = OpenLibrary((CONST_STRPTR)name, version);
		if (lib) *base = lib;
	}
	~LibScope()
	{
		if (lib) { *base = saved; CloseLibrary(lib); }
	}
	explicit operator bool() const { return lib != nullptr; }
};

bool fail(std::string *error, const std::string &m)
{
	if (error) *error = m;
	return false;
}

std::string tempStem(const char *tag)
{
	char name[64];
	std::snprintf(name, sizeof name, "BoxerIcon-%lx-%s", (unsigned long)(IPTR)FindTask(NULL), tag);
	// T: is assigned on every installed AROS; RAM: is the fallback.
	BPTR l = Lock((CONST_STRPTR)"T:", SHARED_LOCK);
	if (l) { UnLock(l); return std::string("T:") + name; }
	return std::string("RAM:") + name;
}

} // namespace

bool loadPicture(const std::string &path, RGBAImage &out, std::string *error)
{
	LibScope dt(&DataTypesBase, "datatypes.library", 43);
	if (!dt) return fail(error, "datatypes.library 43 is not available");
	Object *o = NewDTObject((APTR)path.c_str(), DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43,
	                        PDTA_Remap, FALSE, TAG_DONE);
	if (!o) return fail(error, fsutil::baseName(path) + " is not a picture the installed datatypes can read");
	struct BitMapHeader *bmh = nullptr;
	GetDTAttrs(o, PDTA_BitMapHeader, (IPTR)&bmh, TAG_DONE);
	bool ok = false;
	if (!bmh || !bmh->bmh_Width || !bmh->bmh_Height) {
		fail(error, fsutil::baseName(path) + ": the picture has no size");
	} else if (bmh->bmh_Width > 4096 || bmh->bmh_Height > 4096) {
		fail(error, fsutil::baseName(path) + " is larger than 4096 pixels on a side");
	} else {
		const int w = bmh->bmh_Width, h = bmh->bmh_Height;
		RGBAImage img(w, h);
		ok = DoMethod(o, PDTM_READPIXELARRAY, (IPTR)img.px.data(), PBPAFMT_RGBA, w * 4, 0, 0, w, h) != 0;
		if (!ok) {
			// Some datatypes fill the pixel buffer only after a layout.
			DoDTMethod(o, NULL, NULL, DTM_PROCLAYOUT, (IPTR)NULL, TRUE);
			ok = DoMethod(o, PDTM_READPIXELARRAY, (IPTR)img.px.data(), PBPAFMT_RGBA, w * 4, 0, 0, w, h) != 0;
		}
		if (!ok) {
			fail(error, fsutil::baseName(path) + ": the picture's pixels could not be read");
		} else {
			// Only a picture with an alpha channel has meaningful alpha;
			// picture.datatype leaves other formats' fourth byte as it is.
			if (bmh->bmh_Masking != mskHasAlpha)
				for (size_t i = 3; i < img.px.size(); i += 4) img.px[i] = 255;
			out = std::move(img);
		}
	}
	DisposeDTObject(o);
	return ok;
}

bool readSidecarToolTypes(const std::string &iconStem, std::string &boxerId, std::string &gameboxName)
{
	boxerId.clear();
	gameboxName.clear();
	LibScope icon(&IconBase, "icon.library", 44);
	if (!icon) return false;
	struct DiskObject *dob = GetDiskObject((CONST_STRPTR)iconStem.c_str());
	if (!dob) return false;
	if (auto v = FindToolType((CONST STRPTR *)dob->do_ToolTypes, (STRPTR)"BOXERID")) boxerId = (const char *)v;
	if (auto v = FindToolType((CONST STRPTR *)dob->do_ToolTypes, (STRPTR)"GAMEBOX")) gameboxName = (const char *)v;
	FreeDiskObject(dob);
	return true;
}

bool writeSidecarIcon(const std::string &iconStem, const RGBAImage &image, const std::string &identifier,
                      const std::string &gameboxFileName, bool replaceOwn, std::string *error)
{
	const std::string final = iconStem + ".info";
	if (fsutil::exists(final)) {
		if (!replaceOwn) return fail(error, final + " already exists; it was left unchanged");
		std::string id, gb;
		if (!readSidecarToolTypes(iconStem, id, gb) || id != identifier)
			return fail(error, final + " is not this game's icon; it was left unchanged");
	}
	std::string bytes;
	{
		LibScope icon(&IconBase, "icon.library", 44);
		if (!icon) return fail(error, "icon.library 44 is not available");
		const std::string src = tempStem("src"), dst = tempStem("out");
		if (!fsutil::writeFile(src + ".info", encodePNG(image))) return fail(error, "could not write " + src + ".info");
		struct DiskObject *dob = GetDiskObject((CONST_STRPTR)src.c_str());
		if (!dob) {
			DeleteFile((CONST_STRPTR)(src + ".info").c_str());
			return fail(error, "icon.library could not read the cover as an icon");
		}
		const auto tt = sidecarToolTypes(identifier, gameboxFileName);
		STRPTR types[3] = {(STRPTR)tt[0].c_str(), (STRPTR)tt[1].c_str(), nullptr};
		const UBYTE type = dob->do_Type;
		STRPTR tool = dob->do_DefaultTool, *oldTypes = dob->do_ToolTypes;
		const LONG x = dob->do_CurrentX, y = dob->do_CurrentY;
		dob->do_Type = WBPROJECT;
		dob->do_DefaultTool = (STRPTR)"Boxer:BoxerUI";
		dob->do_ToolTypes = types;
		dob->do_CurrentX = NO_ICON_POSITION;
		dob->do_CurrentY = NO_ICON_POSITION;
		const bool put = PutDiskObject((CONST_STRPTR)dst.c_str(), dob);
		// FreeDiskObject frees what GetDiskObject allocated, not our strings.
		dob->do_Type = type; dob->do_DefaultTool = tool; dob->do_ToolTypes = oldTypes;
		dob->do_CurrentX = x; dob->do_CurrentY = y;
		FreeDiskObject(dob);
		const bool read = put && fsutil::readFile(dst + ".info", bytes);
		DeleteFile((CONST_STRPTR)(src + ".info").c_str());
		DeleteFile((CONST_STRPTR)(dst + ".info").c_str());
		if (!read) return fail(error, "icon.library could not write the icon");
		// A PNG icon read back is written as PNG with the picture kept; any
		// other result would have lost the cover.
		if (bytes.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0)
			return fail(error, "icon.library did not keep the cover picture in the icon");
	}
	std::string err;
	const bool existed = fsutil::exists(final);
	if (existed) {
		if (!fsutil::replaceFile(final, bytes, &err)) return fail(error, "could not replace " + final + ": " + err);
	} else if (!fsutil::writeFile(final, bytes)) {
		DeleteFile((CONST_STRPTR)final.c_str());
		return fail(error, "could not write " + final);
	}
	// Wanderer shows the new or changed icon (PutIconTagList's
	// ICONPUTA_NotifyWorkbench does the same when icon.library writes).
	LibScope wb(&WorkbenchBase, "workbench.library", 44);
	if (wb) {
		BPTR parent = Lock((CONST_STRPTR)fsutil::parent(iconStem).c_str(), SHARED_LOCK);
		if (parent) {
			// A changed icon is removed and added again, so Wanderer reads
			// the new image instead of keeping the one it shows.
			const std::string name = fsutil::baseName(iconStem);
			if (existed) UpdateWorkbench((CONST_STRPTR)name.c_str(), parent, UPDATEWB_ObjectRemoved);
			UpdateWorkbench((CONST_STRPTR)name.c_str(), parent, UPDATEWB_ObjectAdded);
			UnLock(parent);
		}
	}
	return true;
}

} // namespace boxer
