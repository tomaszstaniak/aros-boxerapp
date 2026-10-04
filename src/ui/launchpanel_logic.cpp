// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/DOS window/BXLaunchPanelController.m,
// Boxer/BXSession.m), https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.
#include "launchpanel_logic.h"

#include <algorithm>
#include <cctype>

namespace boxer_ui {

static std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::string upper(std::string s) {
    for (auto &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
// rangeOfString:options:NSCaseInsensitiveSearch; an empty needle is never found.
static bool containsNoCase(const std::string &hay, const std::string &needle) {
    if (needle.empty() || hay.empty()) return false;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

std::string LPRow::subtitle() const {
    return arguments.empty() ? dosPath : dosPath + " " + arguments;
}

std::string dosFilename(const std::string &path) {
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    size_t cut = p.find_last_of("/\\:");
    return upper(cut == std::string::npos ? p : p.substr(cut + 1));
}

LPRow programRow(const std::string &hostPath, const std::string &dosPath, char drive,
                 const std::string &arguments) {
    LPRow r;
    r.kind = LPRow::Program;
    r.title = dosFilename(hostPath);
    if (!arguments.empty()) r.title += " " + arguments;
    r.hostPath = hostPath;
    r.dosPath = dosPath;
    r.drive = drive;
    r.arguments = arguments;
    return r;
}

LPRow favoriteRow(const std::string &title, const std::string &hostPath, const std::string &dosPath,
                  char drive, const std::string &arguments, int launcherIndex) {
    LPRow r;
    r.kind = LPRow::Favorite;
    r.title = title.empty() ? dosFilename(hostPath) : title;
    r.hostPath = hostPath;
    r.dosPath = dosPath;
    r.drive = drive;
    r.arguments = arguments;
    r.launcher = launcherIndex;
    return r;
}

LPRow driveRow(char letter, const std::string &driveTitle, int driveType, const std::string &source) {
    LPRow r;
    r.kind = LPRow::DriveHeading;
    r.title = std::string("Drive ") + letter + " (" + driveTitle + ")";
    r.drive = letter;
    r.hostPath = source;
    r.icon = driveType == 1 ? LPIcon::CDROM : driveType == 2 ? LPIcon::Floppy : LPIcon::HardDisk;
    return r;
}

LPRow sectionHeading(LPIcon which) {
    LPRow r;
    r.kind = LPRow::SectionHeading;
    r.icon = which;
    r.title = which == LPIcon::Favorites ? "Favorites" : which == LPIcon::Recent ? "Recent" : "All Programs";
    return r;
}

std::vector<LPRow> recentRows(const std::vector<LPRecent> &recents,
                              const std::vector<LPLauncherRef> &launchers) {
    std::vector<LPRow> out;
    for (size_t i = 0; i < recents.size(); ++i) {
        if ((int)out.size() >= kMaxRecentRows) break;
        const LPRecent &r = recents[i];
        if (!r.accessible) continue;
        bool matchesLauncher = false;
        for (const auto &l : launchers)
            if (lower(l.hostPath) == lower(r.hostPath) && l.arguments == r.arguments) {
                matchesLauncher = true;
                break;
            }
        if (matchesLauncher) continue;
        LPRow row = programRow(r.hostPath, r.dosPath, r.drive, r.arguments);
        row.recent = (int)i;
        out.push_back(row);
    }
    return out;
}

std::vector<std::string> filterKeywords(const std::string &text) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    std::string cur;
    auto push = [&](const std::string &k) {
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    };
    for (char c : text) {
        if (std::isspace((unsigned char)c)) { push(cur); cur.clear(); }
        else cur += c;
    }
    push(cur);
    return out;
}

int relevance(const LPRow &row, const std::vector<std::string> &keywords) {
    int rel = 0;
    for (const auto &k : keywords) {
        if (containsNoCase(row.title, k)) rel += 3;     // title matches are worth more
        if (containsNoCase(row.dosPath, k)) rel += 2;
    }
    if (rel > 0 && !row.dosPath.empty() && row.dosPath[0] == 'C') rel += 1;
    return rel;
}

std::vector<LPRow> rowsMatching(const std::vector<std::string> &keywords, const std::vector<LPRow> &rows) {
    std::vector<std::pair<int, LPRow>> m;
    for (const auto &r : rows) {
        if (r.isHeading()) continue;
        int rel = relevance(r, keywords);
        if (rel > 0) m.emplace_back(rel, r);
    }
    std::stable_sort(m.begin(), m.end(), [](const std::pair<int, LPRow> &a, const std::pair<int, LPRow> &b) {
        if (a.first != b.first) return a.first > b.first;
        return lower(a.second.dosPath) < lower(b.second.dosPath);
    });
    std::vector<LPRow> out;
    for (auto &p : m) out.push_back(p.second);
    return out;
}

std::vector<LPRow> displayedRows(const std::vector<LPRow> &favorites, const std::vector<LPRow> &recents,
                                 const std::vector<LPRow> &allPrograms,
                                 const std::vector<std::string> &keywords) {
    std::vector<LPRow> f, r, a;
    bool favHead, recHead, allHead;
    if (!keywords.empty()) {
        f = rowsMatching(keywords, favorites);
        r = rowsMatching(keywords, recents);
        a = rowsMatching(keywords, allPrograms);
        favHead = !f.empty() && (!r.empty() || !a.empty());
        recHead = !r.empty() && (!f.empty() || !a.empty());
        allHead = !a.empty() && (!f.empty() || !r.empty());
    } else {
        f = favorites; r = recents; a = allPrograms;
        favHead = !f.empty();
        recHead = !r.empty();
        allHead = false;
    }
    std::vector<LPRow> out;
    if (favHead) out.push_back(sectionHeading(LPIcon::Favorites));
    out.insert(out.end(), f.begin(), f.end());
    if (recHead) out.push_back(sectionHeading(LPIcon::Recent));
    out.insert(out.end(), r.begin(), r.end());
    if (allHead) out.push_back(sectionHeading(LPIcon::AllPrograms));
    out.insert(out.end(), a.begin(), a.end());
    return out;
}

bool startWithLaunchPanel(bool allowsLauncherPanel, bool alwaysShow, bool showOnce) {
    if (!allowsLauncherPanel) return false;
    return alwaysShow || showOnce;
}

Completion afterReturnToShell(Completion requested, bool allowsLauncherPanel, bool internalProcess,
                              double runSeconds, bool loadingPanel) {
    if (requested == Completion::ShowPrompt) return Completion::ShowPrompt;
    if (requested == Completion::ShowLauncher) {
        if (!allowsLauncherPanel) return Completion::ShowPrompt;
        // A program that ran suspiciously briefly may have crashed or shown
        // a message: stay at the DOS prompt.
        if (!internalProcess && runSeconds < kSuccessfulRunSeconds) return Completion::ShowPrompt;
        return Completion::ShowLauncher;
    }
    if (loadingPanel) return allowsLauncherPanel ? Completion::ShowLauncher : Completion::ShowPrompt;
    return Completion::DoNothing;
}

}  // namespace boxer_ui
