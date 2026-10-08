// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "plist.h"

#include "fsutil.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace boxer {

PlistValue PlistValue::string(const std::string &s) { PlistValue v; v.type_ = Type::String; v.s_ = s; return v; }
PlistValue PlistValue::integer(int64_t i) { PlistValue v; v.type_ = Type::Integer; v.i_ = i; return v; }
PlistValue PlistValue::real(double d) { PlistValue v; v.type_ = Type::Real; v.d_ = d; return v; }
PlistValue PlistValue::boolean(bool b) { PlistValue v; v.type_ = Type::Boolean; v.b_ = b; return v; }
PlistValue PlistValue::date(const std::string &s) { PlistValue v; v.type_ = Type::Date; v.s_ = s; return v; }
PlistValue PlistValue::data(const std::string &b) { PlistValue v; v.type_ = Type::Data; v.s_ = b; return v; }
PlistValue PlistValue::array() { PlistValue v; v.type_ = Type::Array; return v; }
PlistValue PlistValue::dict() { PlistValue v; v.type_ = Type::Dict; return v; }

bool PlistValue::asBool(bool fallback) const
{
	switch (type_) {
	case Type::Boolean: return b_;
	case Type::Integer: return i_ != 0;
	case Type::Real: return d_ != 0;
	case Type::String: return s_ == "YES" || s_ == "yes" || s_ == "true" || s_ == "1";
	default: return fallback;
	}
}

int64_t PlistValue::asInteger(int64_t fallback) const
{
	switch (type_) {
	case Type::Integer: return i_;
	case Type::Real: return (int64_t)d_;
	case Type::Boolean: return b_ ? 1 : 0;
	case Type::String: return s_.empty() ? fallback : strtoll(s_.c_str(), nullptr, 10);
	default: return fallback;
	}
}

const PlistValue *PlistValue::get(const std::string &key) const
{
	for (const auto &e : entries_)
		if (e.first == key)
			return &e.second;
	return nullptr;
}

PlistValue *PlistValue::get(const std::string &key)
{
	for (auto &e : entries_)
		if (e.first == key)
			return &e.second;
	return nullptr;
}

void PlistValue::set(const std::string &key, const PlistValue &v)
{
	if (PlistValue *p = get(key))
		*p = v;
	else
		entries_.emplace_back(key, v);
}

bool PlistValue::remove(const std::string &key)
{
	for (auto it = entries_.begin(); it != entries_.end(); ++it)
		if (it->first == key) {
			entries_.erase(it);
			return true;
		}
	return false;
}

bool PlistValue::operator==(const PlistValue &o) const
{
	if (type_ != o.type_)
		return false;
	switch (type_) {
	case Type::Invalid: return true;
	case Type::String: case Type::Date: case Type::Data: return s_ == o.s_;
	case Type::Integer: return i_ == o.i_;
	case Type::Real: return d_ == o.d_;
	case Type::Boolean: return b_ == o.b_;
	case Type::Array: return items_ == o.items_;
	case Type::Dict: return entries_ == o.entries_;
	}
	return false;
}

// --- base64 ---

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string encode64(const std::string &in)
{
	std::string out;
	size_t i = 0;
	for (; i + 2 < in.size(); i += 3) {
		unsigned n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
		out += b64[n >> 18]; out += b64[(n >> 12) & 63]; out += b64[(n >> 6) & 63]; out += b64[n & 63];
	}
	if (i < in.size()) {
		unsigned n = (unsigned char)in[i] << 16;
		if (i + 1 < in.size())
			n |= (unsigned char)in[i + 1] << 8;
		out += b64[n >> 18]; out += b64[(n >> 12) & 63];
		out += i + 1 < in.size() ? b64[(n >> 6) & 63] : '=';
		out += '=';
	}
	return out;
}

static bool decode64(const std::string &in, std::string &out)
{
	out.clear();
	unsigned acc = 0;
	int bits = 0;
	for (char c : in) {
		if (std::isspace((unsigned char)c) || c == '=')
			continue;
		const char *p = strchr(b64, c);
		if (!p || !c)
			return false;
		acc = (acc << 6) | (unsigned)(p - b64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += (char)((acc >> bits) & 0xFF);
		}
	}
	return true;
}

// --- parser ---

namespace {

struct Parser {
	const std::string &t;
	size_t p = 0;
	std::string err;

	explicit Parser(const std::string &text) : t(text) {}

	bool fail(const std::string &m)
	{
		if (err.empty())
			err = m + " at offset " + std::to_string(p);
		return false;
	}

	void skipSpace()
	{
		while (p < t.size() && std::isspace((unsigned char)t[p]))
			p++;
	}

	// Skips whitespace, <?...?>, <!-- -->, <!DOCTYPE ...>.
	bool skipMisc()
	{
		for (;;) {
			skipSpace();
			if (t.compare(p, 2, "<?") == 0) {
				size_t e = t.find("?>", p);
				if (e == std::string::npos) return fail("unterminated <?");
				p = e + 2;
			} else if (t.compare(p, 4, "<!--") == 0) {
				size_t e = t.find("-->", p);
				if (e == std::string::npos) return fail("unterminated comment");
				p = e + 3;
			} else if (t.compare(p, 2, "<!") == 0) {
				size_t e = t.find('>', p);
				if (e == std::string::npos) return fail("unterminated <!");
				p = e + 1;
			} else {
				return true;
			}
		}
	}

	// Reads "<name ...>" or "<name/>"; returns false on anything else.
	bool openTag(std::string &name, bool &empty)
	{
		if (!skipMisc()) return false;
		if (p >= t.size() || t[p] != '<' || (p + 1 < t.size() && t[p + 1] == '/'))
			return fail("expected an element");
		size_t e = t.find('>', p);
		if (e == std::string::npos) return fail("unterminated tag");
		std::string inner = t.substr(p + 1, e - p - 1);
		empty = !inner.empty() && inner.back() == '/';
		if (empty) inner.pop_back();
		size_t sp = inner.find_first_of(" \t\r\n");
		name = inner.substr(0, sp);
		p = e + 1;
		return true;
	}

	bool closeTag(const std::string &name)
	{
		if (!skipMisc()) return false;
		std::string want = "</" + name;
		if (t.compare(p, want.size(), want) != 0)
			return fail("expected </" + name + ">");
		size_t e = t.find('>', p);
		if (e == std::string::npos) return fail("unterminated close tag");
		p = e + 1;
		return true;
	}

	bool peekClose()
	{
		skipMisc();
		return t.compare(p, 2, "</") == 0;
	}

	bool text(const std::string &name, std::string &out)
	{
		size_t e = t.find("</" + name, p);
		if (e == std::string::npos) return fail("unterminated <" + name + ">");
		std::string raw = t.substr(p, e - p);
		p = e;
		out.clear();
		for (size_t i = 0; i < raw.size(); i++) {
			if (raw.compare(i, 9, "<![CDATA[") == 0) {
				size_t ce = raw.find("]]>", i);
				if (ce == std::string::npos) return fail("unterminated CDATA");
				out += raw.substr(i + 9, ce - i - 9);
				i = ce + 2;
			} else if (raw[i] == '&') {
				size_t semi = raw.find(';', i);
				if (semi == std::string::npos) return fail("bad entity");
				std::string ent = raw.substr(i + 1, semi - i - 1);
				if (ent == "lt") out += '<';
				else if (ent == "gt") out += '>';
				else if (ent == "amp") out += '&';
				else if (ent == "quot") out += '"';
				else if (ent == "apos") out += '\'';
				else if (!ent.empty() && ent[0] == '#') {
					unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
						? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
					appendUtf8(out, cp);
				} else return fail("unknown entity &" + ent + ";");
				i = semi;
			} else {
				out += raw[i];
			}
		}
		return closeTag(name);
	}

	static void appendUtf8(std::string &o, unsigned long c)
	{
		if (c < 0x80) o += (char)c;
		else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
		else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
		else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
	}

	bool value(PlistValue &v, int depth)
	{
		if (depth > 64) return fail("nesting too deep");
		std::string name;
		bool empty;
		if (!openTag(name, empty)) return false;
		std::string s;
		if (name == "true" || name == "false") {
			v = PlistValue::boolean(name == "true");
			return empty || closeTag(name);
		}
		if (name == "string") {
			if (!empty && !text(name, s)) return false;
			v = PlistValue::string(empty ? "" : s);
			return true;
		}
		if (name == "integer" || name == "real" || name == "date" || name == "data") {
			if (empty) return fail("empty <" + name + ">");
			if (!text(name, s)) return false;
			if (name == "integer") {
				char *end;
				long long i = strtoll(s.c_str(), &end, 0);
				while (*end && std::isspace((unsigned char)*end)) end++;
				if (*end) return fail("bad integer '" + s + "'");
				v = PlistValue::integer(i);
			} else if (name == "real") {
				v = PlistValue::real(strtod(s.c_str(), nullptr));
			} else if (name == "date") {
				v = PlistValue::date(s);
			} else {
				std::string bytes;
				if (!decode64(s, bytes)) return fail("bad base64 in <data>");
				v = PlistValue::data(bytes);
			}
			return true;
		}
		if (name == "array") {
			v = PlistValue::array();
			if (empty) return true;
			while (!peekClose()) {
				PlistValue item;
				if (!value(item, depth + 1)) return false;
				v.items().push_back(item);
			}
			return closeTag(name);
		}
		if (name == "dict") {
			v = PlistValue::dict();
			if (empty) return true;
			while (!peekClose()) {
				std::string kname;
				bool kempty;
				if (!openTag(kname, kempty) || kname != "key") return fail("expected <key>");
				std::string key;
				if (!kempty && !text("key", key)) return false;
				PlistValue item;
				if (!value(item, depth + 1)) return false;
				v.set(key, item);
			}
			return closeTag(name);
		}
		return fail("unsupported element <" + name + ">");
	}
};

} // namespace

bool parsePlist(const std::string &textIn, PlistValue &out, std::string *error)
{
	if (textIn.compare(0, 6, "bplist") == 0) {
		if (error) *error = "binary property list is not supported; convert it to XML (plutil -convert xml1)";
		return false;
	}
	std::string text = textIn;
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		text.erase(0, 3);
	Parser ps(text);
	std::string name;
	bool empty;
	if (!ps.openTag(name, empty) || name != "plist") {
		if (error) *error = ps.err.empty() ? "not an XML property list (no <plist>)" : ps.err;
		return false;
	}
	PlistValue v;
	if (empty || !ps.value(v, 0) || !ps.closeTag("plist")) {
		if (error) *error = ps.err.empty() ? "empty <plist>" : ps.err;
		return false;
	}
	out = v;
	return true;
}

static std::string escape(const std::string &s)
{
	std::string o;
	for (char c : s) {
		if (c == '<') o += "&lt;";
		else if (c == '>') o += "&gt;";
		else if (c == '&') o += "&amp;";
		else o += c;
	}
	return o;
}

static void write(std::string &o, const PlistValue &v, int depth)
{
	std::string ind(depth, '\t');
	char buf[64];
	switch (v.type()) {
	case PlistValue::Type::Invalid: break;
	case PlistValue::Type::String: o += ind + "<string>" + escape(v.str()) + "</string>\n"; break;
	case PlistValue::Type::Date: o += ind + "<date>" + escape(v.str()) + "</date>\n"; break;
	case PlistValue::Type::Integer:
		snprintf(buf, sizeof buf, "%lld", (long long)v.integer());
		o += ind + "<integer>" + buf + "</integer>\n";
		break;
	case PlistValue::Type::Real:
		snprintf(buf, sizeof buf, "%.17g", v.real());
		o += ind + "<real>" + buf + "</real>\n";
		break;
	case PlistValue::Type::Boolean: o += ind + (v.boolean() ? "<true/>\n" : "<false/>\n"); break;
	case PlistValue::Type::Data: o += ind + "<data>\n" + ind + encode64(v.str()) + "\n" + ind + "</data>\n"; break;
	case PlistValue::Type::Array:
		if (v.items().empty()) { o += ind + "<array/>\n"; break; }
		o += ind + "<array>\n";
		for (const auto &i : v.items()) write(o, i, depth + 1);
		o += ind + "</array>\n";
		break;
	case PlistValue::Type::Dict:
		if (v.entries().empty()) { o += ind + "<dict/>\n"; break; }
		o += ind + "<dict>\n";
		for (const auto &e : v.entries()) {
			o += ind + "\t<key>" + escape(e.first) + "</key>\n";
			write(o, e.second, depth + 1);
		}
		o += ind + "</dict>\n";
		break;
	}
}

std::string writePlist(const PlistValue &root)
{
	std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n";
	write(o, root, 0);
	o += "</plist>\n";
	return o;
}

bool readPlistFile(const std::string &path, PlistValue &out, std::string *error)
{
	fsutil::recoverReplace(path);
	std::string data;
	if (!fsutil::readFile(path, data)) {
		const std::string left = fsutil::describeLeftovers(path);
		if (error) *error = left.empty() ? "cannot read " + path : left;
		return false;
	}
	std::string e;
	if (!parsePlist(data, out, &e)) {
		if (error) *error = path + ": " + e;
		return false;
	}
	return true;
}

bool writePlistFile(const std::string &path, const PlistValue &root, std::string *error)
{
	return fsutil::replaceFile(path, writePlist(root), error);
}

} // namespace boxer
