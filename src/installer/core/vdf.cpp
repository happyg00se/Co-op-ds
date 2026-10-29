#include "vdf.hpp"

#include <cctype>

namespace dsinst::vdf {
namespace {
bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

struct Lexer {
    std::string_view s;
    size_t i = 0;
    enum class Tok { End, Str, Open, Close, Error };

    void skip() {
        for (;;) {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            if (i < s.size() && s[i] == '[') {  // conditionals like [$WIN32] are ignored
                while (i < s.size() && s[i] != ']') ++i;
                if (i < s.size()) ++i;
                continue;
            }
            break;
        }
    }
    Tok next(std::string& out) {
        skip();
        if (i >= s.size()) return Tok::End;
        const char c = s[i];
        if (c == '{') { ++i; return Tok::Open; }
        if (c == '}') { ++i; return Tok::Close; }
        out.clear();
        if (c == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    const char e = s[i + 1];
                    out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
                    i += 2;
                } else {
                    out.push_back(s[i++]);
                }
            }
            if (i >= s.size()) return Tok::Error;
            ++i;
            return Tok::Str;
        }
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '{' && s[i] != '}' && s[i] != '"') out.push_back(s[i++]);
        return Tok::Str;
    }
};

bool parseObject(Lexer& lx, Node& obj, bool topLevel, std::string* err, int depth) {
    if (depth > 64) { if (err) *err = "nesting too deep"; return false; }
    std::string tok;
    for (;;) {
        auto t = lx.next(tok);
        if (t == Lexer::Tok::End) {
            if (topLevel) return true;
            if (err) *err = "unexpected end of file";
            return false;
        }
        if (t == Lexer::Tok::Close) {
            if (topLevel) { if (err) *err = "unexpected '}'"; return false; }
            return true;
        }
        if (t != Lexer::Tok::Str) { if (err) *err = "expected key"; return false; }
        Node child;
        child.key = tok;
        std::string val;
        auto t2 = lx.next(val);
        if (t2 == Lexer::Tok::Open) {
            child.isObject = true;
            if (!parseObject(lx, child, false, err, depth + 1)) return false;
        } else if (t2 == Lexer::Tok::Str) {
            child.value = val;
        } else {
            if (err) *err = "expected value for key '" + child.key + "'";
            return false;
        }
        obj.children.push_back(std::move(child));
    }
}
}  // namespace

const Node* Node::find(std::string_view k) const {
    for (const auto& c : children) if (ieq(c.key, k)) return &c;
    return nullptr;
}

std::string Node::get(std::string_view k, std::string def) const {
    const Node* n = find(k);
    return n && !n->isObject ? n->value : def;
}

std::optional<Node> parse(std::string_view text, std::string* err) {
    // Strip UTF-8 BOM.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);
    Lexer lx{text};
    Node root;
    root.isObject = true;
    if (!parseObject(lx, root, true, err, 0)) return std::nullopt;
    return root;
}

}  // namespace dsinst::vdf
