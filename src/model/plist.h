// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Minimal XML property list reader and writer (Apple PropertyList-1.0 DTD),
// enough for a gamebox's "Game Info.plist" and a game state's Info.plist.
// The original used NSDictionary dictionaryWithContentsOfURL /
// writeToURL:atomically: (BXGamebox _persistGameInfo). Dictionaries keep
// their key order and every key, known or not, so a rewrite only changes
// what the caller changed. Binary plists ("bplist00") are rejected with a
// clear error rather than misread.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace boxer {

class PlistValue {
public:
	enum class Type { Invalid, String, Integer, Real, Boolean, Date, Data, Array, Dict };

	PlistValue() = default;
	static PlistValue string(const std::string &s);
	static PlistValue integer(int64_t i);
	static PlistValue real(double d);
	static PlistValue boolean(bool b);
	static PlistValue date(const std::string &iso8601); // kept verbatim
	static PlistValue data(const std::string &bytes);
	static PlistValue array();
	static PlistValue dict();

	Type type() const { return type_; }
	bool isValid() const { return type_ != Type::Invalid; }

	const std::string &str() const { return s_; }  // String, Date, Data bytes
	int64_t integer() const { return i_; }
	double real() const { return d_; }
	bool boolean() const { return b_; }
	// Loose numeric/boolean reading as NSNumber would allow.
	bool asBool(bool fallback = false) const;
	int64_t asInteger(int64_t fallback = 0) const;

	std::vector<PlistValue> &items() { return items_; }
	const std::vector<PlistValue> &items() const { return items_; }

	// Dictionary access; null when absent or not a dict.
	const PlistValue *get(const std::string &key) const;
	PlistValue *get(const std::string &key);
	void set(const std::string &key, const PlistValue &v); // keeps position if present
	bool remove(const std::string &key);
	const std::vector<std::pair<std::string, PlistValue>> &entries() const { return entries_; }

	bool operator==(const PlistValue &o) const;
	bool operator!=(const PlistValue &o) const { return !(*this == o); }

private:
	Type type_ = Type::Invalid;
	std::string s_;
	int64_t i_ = 0;
	double d_ = 0;
	bool b_ = false;
	std::vector<PlistValue> items_;
	std::vector<std::pair<std::string, PlistValue>> entries_;
};

bool parsePlist(const std::string &text, PlistValue &out, std::string *error = nullptr);
std::string writePlist(const PlistValue &root);

bool readPlistFile(const std::string &path, PlistValue &out, std::string *error = nullptr);
// Writes through fsutil::replaceFile.
bool writePlistFile(const std::string &path, const PlistValue &root, std::string *error = nullptr);

} // namespace boxer
