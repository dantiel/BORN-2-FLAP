#pragma once
// Born2FlapUiOps.h — the pure, dependency-free wire model + JSON parser.
//
// This is the C++ half of the Ruby `Wire` format (docs/ui-wire-format.md). It
// has NO Unreal dependency: the same header compiles inside the UMG host and
// inside the headless `Tools/umg_host_test.cpp` (clang++). The op stream is the
// HostConfig contract — five discrete tree mutations plus set_material_params:
//
//   {"op":"create_instance","path":[],"node":{"type":"Overlay","props":{...},"children":[...]}}
//   {"op":"remove_instance","path":[0]}
//   {"op":"append_child","path":[0],"index":1,"node":{...}}
//   {"op":"remove_child","path":[0],"index":1}
//   {"op":"update_props","path":[0,0],"props":{"text":"yo"}}
//   {"op":"set_material_params","path":[0],"params":{"BlurRadius":0.5}}
//
// `path` is a list of child indices from the root (root itself is `[]`).

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace born2flap::ui {

// A JSON scalar carried by props / material params. Supports null/bool/number/
// string/array — enough for the Ruby wire (text, min/max/value, bind, onPress,
// material scalar names).
struct FValue {
    enum class Kind : uint8_t { Null, Bool, Number, String, Array } kind = Kind::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<FValue> arr;

    std::string AsString() const { return kind == Kind::String ? str : std::string(); }
    double AsNumber(double dflt = 0.0) const { return kind == Kind::Number ? num : dflt; }
    bool AsBool(bool dflt = false) const { return kind == Kind::Bool ? b : dflt; }
};

using FProps = std::map<std::string, FValue>;
using FPath  = std::vector<int>;

struct FNode {
    std::string type;
    FProps props;
    std::vector<FNode> children;
};

enum class EOp : uint8_t {
    CreateInstance, RemoveInstance, AppendChild, RemoveChild, UpdateProps, SetMaterialParams
};

struct FOp {
    EOp op = EOp::CreateInstance;
    FPath path;
    int index = -1;
    FNode node;
    FProps props;  // update_props → props; set_material_params → params
};

// ---- minimal recursive-descent JSON parser (no external deps) -------------
namespace detail {

class FJsonParser {
public:
    explicit FJsonParser(const std::string& src) : s(src), i(0) {}

    bool ParseValue(FValue& out) {
        SkipWs();
        if (i >= s.size()) return Fail("unexpected end");
        const char c = s[i];
        if (c == '{') return ParseObject(out);
        if (c == '[') return ParseArray(out);
        if (c == '"') { out.kind = FValue::Kind::String; return ParseString(out.str); }
        if (c == '-' || (c >= '0' && c <= '9')) { out.kind = FValue::Kind::Number; return ParseNumber(out.num); }
        if (Match("true"))  { out.kind = FValue::Kind::Bool; out.b = true;  return true; }
        if (Match("false")) { out.kind = FValue::Kind::Bool; out.b = false; return true; }
        if (Match("null"))  { out.kind = FValue::Kind::Null; return true; }
        return Fail("unexpected token");
    }

    bool ParseObject(FValue& out) {
        out.kind = FValue::Kind::String;  // object content is returned via key maps; not stored in FValue
        // We only ever parse objects into maps via ParseObjectMap, so a bare
        // object here is only valid transiently. Reject to keep semantics clear.
        return Fail("naked object unsupported (use ParseObjectMap)");
    }

    // Parse a JSON object into a string→FValue map. Used for props/params.
    bool ParseObjectMap(FProps& out) {
        SkipWs();
        if (!Expect('{')) return false;
        SkipWs();
        if (Consume('}')) return true;
        while (true) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (!Expect(':')) return false;
            FValue val;
            if (!ParseValue(val)) return false;
            out[key] = std::move(val);
            SkipWs();
            if (Consume('}')) return true;
            if (!Expect(',')) return false;
        }
    }

    bool ParseNode(FNode& node) {
        SkipWs();
        if (!Expect('{')) return false;
        SkipWs();
        if (Consume('}')) return true;
        while (true) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (!Expect(':')) return false;
            if (key == "type") { if (!ParseString(node.type)) return false; }
            else if (key == "props") { if (!ParseObjectMap(node.props)) return false; }
            else if (key == "children") { if (!ParseNodeArray(node.children)) return false; }
            else { FValue dummy; if (!ParseValue(dummy)) return false; }
            SkipWs();
            if (Consume('}')) return true;
            if (!Expect(',')) return false;
        }
    }

    bool ParseNodeArray(std::vector<FNode>& out) {
        SkipWs();
        if (!Expect('[')) return false;
        SkipWs();
        if (Consume(']')) return true;
        while (true) {
            FNode node;
            if (!ParseNode(node)) return false;
            out.push_back(std::move(node));
            SkipWs();
            if (Consume(']')) return true;
            if (!Expect(',')) return false;
        }
    }

    bool ParsePath(FPath& out) {
        SkipWs();
        if (!Expect('[')) return false;
        SkipWs();
        if (Consume(']')) return true;
        while (true) {
            FValue v;
            if (!ParseValue(v)) return false;
            out.push_back((int)v.AsNumber(0.0));
            SkipWs();
            if (Consume(']')) return true;
            if (!Expect(',')) return false;
        }
    }

    bool ParseOps(std::vector<FOp>& out) {
        SkipWs();
        if (!Expect('[')) return false;
        SkipWs();
        if (Consume(']')) return true;
        while (true) {
            FOp op;
            if (!ParseOp(op)) return false;
            out.push_back(std::move(op));
            SkipWs();
            if (Consume(']')) return true;
            if (!Expect(',')) return false;
        }
    }

    const std::string& Error() const { return err; }

private:
    bool ParseOp(FOp& op) {
        SkipWs();
        if (!Expect('{')) return false;
        SkipWs();
        if (Consume('}')) return true;
        while (true) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (!Expect(':')) return false;
            if (key == "op") {
                std::string name;
                if (!ParseString(name)) return false;
                if (name == "create_instance")      op.op = EOp::CreateInstance;
                else if (name == "remove_instance") op.op = EOp::RemoveInstance;
                else if (name == "append_child")    op.op = EOp::AppendChild;
                else if (name == "remove_child")    op.op = EOp::RemoveChild;
                else if (name == "update_props")    op.op = EOp::UpdateProps;
                else if (name == "set_material_params") op.op = EOp::SetMaterialParams;
                else return Fail("unknown op: " + name);
            }
            else if (key == "path") { if (!ParsePath(op.path)) return false; }
            else if (key == "index") { FValue v; if (!ParseValue(v)) return false; op.index = (int)v.AsNumber(0.0); }
            else if (key == "node") { if (!ParseNode(op.node)) return false; }
            else if (key == "props" || key == "params") { if (!ParseObjectMap(op.props)) return false; }
            else { FValue dummy; if (!ParseValue(dummy)) return false; }
            SkipWs();
            if (Consume('}')) return true;
            if (!Expect(',')) return false;
        }
    }

    bool ParseArray(FValue& out) {
        out.kind = FValue::Kind::Array;
        SkipWs();
        if (!Expect('[')) return false;
        SkipWs();
        if (Consume(']')) return true;
        while (true) {
            FValue v;
            if (!ParseValue(v)) return false;
            out.arr.push_back(std::move(v));
            SkipWs();
            if (Consume(']')) return true;
            if (!Expect(',')) return false;
        }
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (!Expect('"')) return false;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) return Fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return Fail("bad unicode escape");
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                            else return Fail("bad hex");
                        }
                        // UTF-8 encode the code point.
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: return Fail("bad escape char");
                }
            } else {
                out += c;
            }
        }
        return Fail("unterminated string");
    }

    bool ParseNumber(double& out) {
        size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i < s.size() && s[i] == '.') { ++i; while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i; }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i == start) return Fail("bad number");
        out = std::strtod(s.c_str() + start, nullptr);
        return true;
    }

    void SkipWs() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool Consume(char c) { SkipWs(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    bool Expect(char c) { SkipWs(); if (i < s.size() && s[i] == c) { ++i; return true; } return Fail(std::string("expected '") + c + "'"); }
    bool Match(const char* lit) {
        SkipWs();
        size_t n = 0; while (lit[n]) ++n;
        if (s.compare(i, n, lit) == 0) { i += n; return true; }
        return false;
    }
    bool Fail(const std::string& msg) { if (err.empty()) err = msg; return false; }

    const std::string& s;
    size_t i = 0;
    std::string err;
};

}  // namespace detail

// Parse the Ruby `Wire.dump_ops` JSON array into ops. Returns false on error.
inline bool ParseOps(const std::string& json, std::vector<FOp>& out) {
    detail::FJsonParser p(json);
    return p.ParseOps(out);
}

}  // namespace born2flap::ui
