// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Session setup for a gamebox: BXSession's preparation reduced to what the
// embedded core needs (identifier, data directory, shadow mappings,
// configuration layering, preflight mounts, default launcher). Shared by the
// Zune session window (src/ui) and the core smoke host.
#pragma once

#include "../../emulator/emulator.h"
#include "../../model/gamebox.h"
#include "../../model/shadowfs.h"

#include <functional>
#include <string>
#include <vector>

namespace boxer {

using SetupLog = std::function<void(const std::string &)>;

// Launcher path (relative to the gamebox) -> DOS path on its bundled drive.
bool dosPathForLauncher(const std::vector<BundledDrive> &drives, const std::string &gamebox,
                        const std::string &relative, std::string &dosPath);

// confs holds extra (optional) configuration files on entry; on success it
// holds the full layered list, with the extras after the gamebox's own conf
// and before Launch.conf. launch is filled with the default launcher only
// when it is empty on entry. gameName receives the gamebox's display name.
bool openGamebox(const SetupLog &log, const std::string &path, const std::string &dataDir,
                 const std::string &confDir, std::vector<ConfigFile> &confs,
                 std::vector<std::string> &preflight, std::vector<std::string> &launch,
                 ShadowFileSystem &shadow, std::string &error, std::string *gameName = nullptr,
                 bool shadowWrites = true);
// shadowWrites false: an import's installer run. Writes go into the
// gamebox itself (BXImportSession _shouldShadowDrive: NO, IS:1414), so no
// shadow mapping is added and the caller uses a plain FileSystem.

} // namespace boxer
