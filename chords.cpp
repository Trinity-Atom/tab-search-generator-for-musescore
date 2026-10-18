#include "chords.hpp"

#include <map>
#include <set>
#include <utility>

namespace tabs {
namespace {
void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

ChordPitch pitch(const std::string& text, size_t& pos) {
    const std::pair<const char*, char> names[] = {
        {"SOL", 'G'}, {"DO", 'C'}, {"RE", 'D'}, {"MI", 'E'},
        {"FA", 'F'}, {"LA", 'A'}, {"SI", 'B'}
    };
    ChordPitch result;
    for (const auto& name : names) {
        const std::string prefix = name.first;
        if (text.compare(pos, prefix.size(), prefix) == 0) {
            result.step = name.second;
            pos += prefix.size();
            break;
        }
    }
    if (!result.step) {
        if (pos >= text.size() || text[pos] < 'A' || text[pos] > 'G')
            throw ChordParseError("Raiz o bajo no valido: " + text);
        result.step = text[pos++];
    }
    if (pos < text.size() && (text[pos] == '#' || text[pos] == 'b'))
        result.alter = text[pos++] == '#' ? 1 : -1;
    return result;
}

std::string pitchText(const ChordPitch& value) {
    return std::string(1, value.step) + (value.alter == 1 ? "#" : value.alter == -1 ? "b" : "");
}

struct Base {
    std::string kind;
    std::set<int> degrees;
};

const std::map<std::string, Base>& bases() {
    static const std::map<std::string, Base> values = {
        {"", {"major", {1,3,5}}}, {"m", {"minor", {1,3,5}}},
        {"5", {"power", {1,5}}}, {"6", {"major-sixth", {1,3,5,6}}},
        {"m6", {"minor-sixth", {1,3,5,6}}}, {"7", {"dominant", {1,3,5,7}}},
        {"maj7", {"major-seventh", {1,3,5,7}}}, {"m7", {"minor-seventh", {1,3,5,7}}},
        {"mMaj7", {"major-minor", {1,3,5,7}}},
        {"9", {"dominant-ninth", {1,3,5,7,9}}}, {"maj9", {"major-ninth", {1,3,5,7,9}}},
        {"m9", {"minor-ninth", {1,3,5,7,9}}},
        {"11", {"dominant-11th", {1,3,5,7,9,11}}},
        {"maj11", {"major-11th", {1,3,5,7,9,11}}}, {"m11", {"minor-11th", {1,3,5,7,9,11}}},
        {"13", {"dominant-13th", {1,3,5,7,9,11,13}}},
        {"maj13", {"major-13th", {1,3,5,7,9,11,13}}}, {"m13", {"minor-13th", {1,3,5,7,9,11,13}}},
        {"dim", {"diminished", {1,3,5}}}, {"dim7", {"diminished-seventh", {1,3,5,7}}},
        {"m7b5", {"half-diminished", {1,3,5,7}}},
        {"aug", {"augmented", {1,3,5}}}, {"aug7", {"augmented-seventh", {1,3,5,7}}},
        {"sus2", {"suspended-second", {1,2,5}}},
        {"sus4", {"suspended-fourth", {1,4,5}}},
        {"7sus2", {"suspended-second", {1,2,5}}},
        {"7sus4", {"suspended-fourth", {1,4,5}}}
    };
    return values;
}
} // namespace

ParsedChord parseChord(const std::string& input) {
    if (input.empty() || input.size() > 128)
        throw ChordParseError("Acorde vacio o demasiado largo");
    std::string text = input;
    replaceAll(text, "\xE2\x99\xAF", "#");
    replaceAll(text, "\xE2\x99\xAD", "b");
    replaceAll(text, "\xC2\xB0", "dim");
    replaceAll(text, "\xC3\xB8", "m7b5");
    if (text.find_first_of(" \t\r\n") != std::string::npos)
        throw ChordParseError("Usa un acorde por fila, sin espacios: " + input);
    ParsedChord result;
    result.original = input;
    size_t pos = 0;
    result.root = pitch(text, pos);
    auto suffix = text.substr(pos);
    const auto slash = suffix.find('/');
    if (slash != std::string::npos) {
        const auto bass = suffix.substr(slash + 1);
        size_t bassPos = 0;
        result.bass = pitch(bass, bassPos);
        if (bassPos != bass.size()) throw ChordParseError("Bajo no valido: " + input);
        suffix.erase(slash);
    }
    if (suffix.rfind("min", 0) == 0) suffix.replace(0, 3, "m");
    if (suffix.size() > 2 && suffix.rfind("mM", 0) == 0 && suffix[2] >= '0' && suffix[2] <= '9')
        suffix.replace(0, 2, "mMaj");
    else if (suffix.rfind("M", 0) == 0) suffix.replace(0, 1, "maj");
    if (suffix.rfind("+", 0) == 0) suffix.replace(0, 1, "aug");
    result.suffix = suffix;
    const Base* base = nullptr;
    std::string baseName;
    for (const auto& entry : bases()) {
        if (suffix.rfind(entry.first, 0) == 0 &&
            (!base || entry.first.size() > baseName.size())) {
            base = &entry.second;
            baseName = entry.first;
        }
    }
    result.kind = base->kind;
    if (baseName == "7sus2" || baseName == "7sus4")
        result.degrees.push_back({7, -1, "add"});
    size_t cursor = baseName.size();
    bool parenthesized = false;
    bool hasParenthesisToken = false;
    bool expectToken = false;
    std::set<int> modified;
    if (baseName == "7sus2" || baseName == "7sus4") modified.insert(7);
    while (cursor < suffix.size()) {
        const char c = suffix[cursor];
        if (c == '(') {
            if (parenthesized) throw ChordParseError("Parentesis anidados: " + input);
            parenthesized = true;
            hasParenthesisToken = false;
            expectToken = true;
            ++cursor;
            continue;
        }
        if (c == ')') {
            if (!parenthesized || !hasParenthesisToken || expectToken)
                throw ChordParseError("Parentesis no validos: " + input);
            parenthesized = false;
            ++cursor;
            continue;
        }
        if (c == ',') {
            if (!parenthesized || !hasParenthesisToken || expectToken)
                throw ChordParseError("Separador no valido: " + input);
            expectToken = true;
            ++cursor;
            continue;
        }
        bool add = suffix.compare(cursor, 3, "add") == 0;
        int alter = 0;
        if (add) cursor += 3;
        if (cursor < suffix.size() && (suffix[cursor] == '#' || suffix[cursor] == 'b'))
            alter = suffix[cursor++] == '#' ? 1 : -1;
        else if (!add) throw ChordParseError("Sufijo no compatible: " + input);
        const auto start = cursor;
        while (cursor < suffix.size() && suffix[cursor] >= '0' && suffix[cursor] <= '9') ++cursor;
        const auto number = suffix.substr(start, cursor - start);
        static const std::set<std::string> allowed{"2","4","5","6","7","9","11","13"};
        if (!allowed.count(number)) throw ChordParseError("Grado no compatible: " + input);
        const int degree = std::stoi(number);
        if (!modified.insert(degree).second || (add && base->degrees.count(degree)) ||
            (alter == 0 && !add))
            throw ChordParseError("Grado duplicado o contradictorio: " + input);
        result.degrees.push_back({degree, alter, base->degrees.count(degree) ? "alter" : "add"});
        hasParenthesisToken = true;
        expectToken = false;
    }
    if (parenthesized || expectToken) throw ChordParseError("Parentesis incompletos: " + input);
    result.normalized = pitchText(result.root) + result.suffix;
    if (result.bass) result.normalized += "/" + pitchText(*result.bass);
    return result;
}

std::string normalizeChord(const std::string& text) {
    try {
        return parseChord(text).normalized;
    } catch (const ChordParseError&) {
        return {};
    }
}
} // namespace tabs
