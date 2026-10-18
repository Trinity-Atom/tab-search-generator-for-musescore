#pragma once

#include <string>
#include <vector>

struct ChordLine {
    std::string chord;
    std::string lyric;
};

struct TabVersion {
    int id{};
    std::string provider;
    std::string title;
    std::string artist;
    std::string versionName;
    std::string sourceUrl;
    int capo{0};
    std::vector<ChordLine> lines;
};
