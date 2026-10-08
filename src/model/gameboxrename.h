// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXImportSession.m setGameboxName:, validateGameboxName:),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Renaming a gamebox together with its sidecar icon, and finding the
// gamebox of a sidecar icon whose name no longer matches.
//
// AROS: a gamebox "Name.boxer" is opened from the lone icon "Name.info"
// beside it, so the two are renamed as a pair. The game's state lives in
// the data directory under the gamebox identifier, which a rename does
// not touch, so saves stay with the game.
//
// The pair stays together: if the icon cannot follow the gamebox, the gamebox
// is renamed back; if even that fails, the state is reported, and the icon,
// which carries BOXERID=<identifier>, still finds its gamebox on the
// next start. Several matches, or an identifier that disagrees with the
// icon's GAMEBOX name, are for the user to decide, never guessed.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace boxer {

enum class NameCheck { Ok, Unchanged, Empty, TooLong, Taken };

// validateGameboxName: (IS:526-571) with the AROS collision rule of
// createGamebox: "<name>.boxer", a plain "<name>" (file or drawer) and
// "<name>.info" must all be free, unless the new name differs from the
// current one only in case (then the first and last are this gamebox and
// its own icon). sanitised gets validGameboxName(requested); message a
// sentence for the requester.
NameCheck checkGameboxRename(const std::string &gameboxPath, const std::string &requested,
                             std::string &sanitised, std::string *message = nullptr);

// Test hook: consulted before each step; true makes that step fail as if
// the file system had refused it.
enum class RenameStep { MoveGamebox, MoveIcon, RestoreGamebox, UpdateIcon };
using RenameFaultHook = std::function<bool(RenameStep)>;
void setRenameFaultHook(RenameFaultHook hook);

struct RenameOutcome {
	enum Kind {
		Renamed,         // gamebox and icon (if it had one) carry the new name
		NotRenamed,      // nothing was changed (refused, or the first step failed)
		RolledBack,      // the icon could not follow; the gamebox has its old name again
		IconLeftBehind,  // gamebox renamed, icon still under the old name (reported)
	} kind = NotRenamed;
	std::string gameboxPath;   // where the gamebox is now
	std::string iconStem;      // its icon without ".info", "" when it has none
	bool iconUpdated = false;  // updateIcon ran and succeeded
	std::string message;       // for the requester; empty after a clean rename
};

// setGameboxName: (IS:457-523) for the pair. A case-only change goes
// through a temporary name, as the original does (IS:474-485). After both
// moves updateIcon(newIconStem) rewrites the icon's GAMEBOX ToolType (and a
// bootleg cover's title); its failure leaves a working pair and is reported.
// An icon is renamed only when "<old>.info" is a lone icon file beside the
// gamebox; a gamebox without one is renamed alone.
RenameOutcome renameGameboxPair(const std::string &gameboxPath, const std::string &newName,
                                const std::function<bool(const std::string &iconStem, std::string *error)> &updateIcon);

// Gameboxes directly inside folder whose BXGameIdentifier is identifier.
std::vector<std::string> gameboxesWithIdentifier(const std::string &folder, const std::string &identifier);

// For a lone icon "<folder>/<stem>.info" whose "<stem>.boxer" is missing:
// the gamebox its GAMEBOX ToolType names (a file name in the same folder),
// and every gamebox in the folder carrying its BOXERID.
struct SidecarCandidate {
	std::string path;
	bool identifierMatches = false;   // its identifier equals BOXERID
	bool namedByIcon = false;         // it is the icon's GAMEBOX
};
std::vector<SidecarCandidate> locateSidecarGamebox(const std::string &folder, const std::string &gameboxToolType,
                                                   const std::string &boxerId);
// True when the lookup is unambiguous: one candidate whose identifier
// matches the icon's BOXERID. Anything else is a question for the user.
bool sidecarLookupIsCertain(const std::vector<SidecarCandidate> &candidates);

// Gives the stale icon "<iconStem>.info" the name of the gamebox it was
// found for, if that name has no icon yet, then runs updateIcon. Never
// replaces an existing icon.
bool repairSidecarName(const std::string &iconStem, const std::string &gameboxPath,
                       const std::function<bool(const std::string &iconStem, std::string *error)> &updateIcon,
                       std::string *error = nullptr);

} // namespace boxer
