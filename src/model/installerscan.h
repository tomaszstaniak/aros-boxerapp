// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXInstallerScan.m, BXImportSession+BXImportPolicies.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// The import's installer scan (BXInstallerScan performScan /
// matchAgainstPath:) and the import policies it relies on, with the
// original's patterns verbatim.
//
// AROS: no game profiles (GameProfiles.plist is not ported), so the
// original's !detectedProfile branches apply. No Mac application detection
// (no .app bundles on AROS media); a folder with only such content falls
// to "no executables". The scan runs synchronously.
#pragma once

#include <string>
#include <vector>

namespace boxer {

class FileSystem;

// BXImportPolicies.
bool isInstallerPath(const std::string &relativePath);           // isInstallerAtPath:
bool isIgnoredImportPath(const std::string &relativePath);       // isIgnoredFileAtPath:
bool isJunkImportPath(const std::string &relativePath);          // isJunkFileAtPath:
bool isPlayableGameTelltale(const std::string &relativePath);    // isPlayableGameTelltaleAtPath:
bool isInconclusiveDOSProgram(const std::string &relativePath);  // isInconclusiveDOSProgramAtPath:
// preferredInstallerFromPaths: "" when none matches.
std::string preferredInstaller(const std::vector<std::string> &relativePaths);
// pathDepthCompare: fewer components first; equal depth keeps order.
void sortByPathDepth(std::vector<std::string> &relativePaths);

enum class InstallerScanError {
	None,
	NoExecutables,  // BXImportNoExecutablesError
	WindowsOnly,    // BXImportWindowsOnlyError
};

struct InstallerScanResult {
	// Paths relative to the scanned folder, '/'-separated.
	std::vector<std::string> installers;        // matchingPaths: preferred first, then by depth
	std::vector<std::string> dosExecutables;
	std::vector<std::string> windowsExecutables;
	std::vector<std::string> dosboxConfigurations;
	bool alreadyInstalled = false;
	InstallerScanError error = InstallerScanError::None;

	// installerScanDidFinish: (IS:193) asks for an installer only when the
	// game is not already installed and installers were found; otherwise
	// the original skips the installer.
	bool shouldOfferInstallers() const { return !alreadyInstalled && !installers.empty(); }
};

InstallerScanResult scanForInstallers(FileSystem &fs, const std::string &root);

// The user-facing sentence of a scan error (BXSessionError.m), naming the
// source folder by its display name.
std::string installerScanErrorText(InstallerScanError error, const std::string &sourceName);

} // namespace boxer
