#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tabs {
struct ChordPitch {
    char step{};
    int alter{};
};

struct ChordDegree {
    int value{};
    int alter{};
    std::string type;
};

struct ParsedChord {
    ChordPitch root;
    std::optional<ChordPitch> bass;
    std::string original;
    std::string normalized;
    std::string suffix;
    std::string kind;
    std::vector<ChordDegree> degrees;
};

class ChordParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

ParsedChord parseChord(const std::string& text);
std::string normalizeChord(const std::string& text);
} // namespace tabs
