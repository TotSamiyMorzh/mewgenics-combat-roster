#include "gon.h"

namespace cr {
namespace {

struct Lexer {
    const char* p;
    const char* end;

    void skip() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ':') { ++p; continue; }
            if (c == '/' && p + 1 < end && p[1] == '/') { while (p < end && *p != '\n') ++p; continue; }
            if (c == '/' && p + 1 < end && p[1] == '*') {
                p += 2;
                while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) ++p;
                p = p + 2 < end ? p + 2 : end;
                continue;
            }
            if (c == '#') { while (p < end && *p != '\n') ++p; continue; }
            break;
        }
    }
    // Returns false at end. `tok` is a punctuation char or a word/quoted string.
    bool next(std::string& tok, char& punct) {
        skip();
        punct = 0;
        tok.clear();
        if (p >= end) return false;
        char c = *p;
        if (c == '{' || c == '}' || c == '[' || c == ']') { punct = c; ++p; return true; }
        if (c == '"') {
            ++p;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) { ++p; tok += *p == 'n' ? '\n' : *p; ++p; continue; }
                tok += *p++;
            }
            if (p < end) ++p;
            return true;
        }
        while (p < end) {
            c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ':' || c == '{' || c == '}' ||
                c == '[' || c == ']' || c == '"')
                break;
            if (c == '/' && p + 1 < end && (p[1] == '/' || p[1] == '*')) break;
            tok += c;
            ++p;
        }
        return true;
    }
};

void parse_body(Lexer& lx, Gon& into, char close, int depth);

void parse_value(Lexer& lx, Gon& node, int depth) {
    std::string tok;
    char punct;
    if (!lx.next(tok, punct)) return;
    if (punct == '{') { node.is_obj = true; parse_body(lx, node, '}', depth + 1); }
    else if (punct == '[') { node.is_arr = true; parse_body(lx, node, ']', depth + 1); }
    else node.value = tok;
}

// Objects hold `key value` pairs; arrays hold bare values.
void parse_body(Lexer& lx, Gon& into, char close, int depth) {
    if (depth > 64) return;
    std::string tok;
    char punct;
    while (true) {
        const char* save = lx.p;
        if (!lx.next(tok, punct)) return;
        if (punct && punct == close) return;
        if (into.is_arr) {
            lx.p = save;
            Gon v;
            parse_value(lx, v, depth);
            into.kids.push_back(std::move(v));
            continue;
        }
        if (punct) continue;   // stray bracket in an object: ignore
        Gon kv;
        kv.key = tok;
        parse_value(lx, kv, depth);
        into.kids.push_back(std::move(kv));
    }
}

}  // namespace

Gon gon_parse(const char* text, size_t len) {
    Gon root;
    root.is_obj = true;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB) { text += 3; len -= 3; }
    Lexer lx{text, text + len};
    parse_body(lx, root, 0, 0);
    return root;
}

}  // namespace cr
