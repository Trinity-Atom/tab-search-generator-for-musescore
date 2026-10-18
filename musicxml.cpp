#include "musicxml.hpp"
#include "chords.hpp"

#include <fstream>
#include <string>
#include <vector>

namespace tabs {
namespace {
std::string sanitizeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\'': out += "&apos;"; break;
            case '\"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::vector<ParsedChord> parseChords(const TabVersion& version) {
    std::vector<ParsedChord> result;
    for (size_t i = 0; i < version.lines.size(); ++i) {
        const auto& chord = version.lines[i].chord;
        if (chord.empty()) continue;
        try {
            result.push_back(parseChord(chord));
        } catch (const ChordParseError& error) {
            throw ChordParseError("Fila " + std::to_string(i + 1) + ": " + error.what());
        }
    }
    return result;
}
} // namespace

bool writeMusicXml(const TabVersion& v, std::ostream& f) {
    // Preserve the console export: one chord per beat with a C4 placeholder.
    const auto chords = parseChords(v);
    if (chords.empty()) return false;
    const int beatsPerMeasure = 4;
    const int measureCount = static_cast<int>((chords.size() + beatsPerMeasure - 1) / beatsPerMeasure);
    f << R"(<?xml version="1.0" encoding="UTF-8" standalone="no"?>)" << "\n";
    f << R"(<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">)" << "\n";
    f << R"(<score-partwise version="4.0">)" << "\n";
    f << "  <work><work-title>" << sanitizeXml(v.title + " - " + v.versionName) << "</work-title></work>\n";
    f << "  <identification>\n";
    f << "    <creator type=\"composer\">" << sanitizeXml(v.artist) << "</creator>\n";
    f << "    <encoding><software>C++ Prototype Chord Exporter</software></encoding>\n";
    f << "  </identification>\n";
    f << "  <part-list>\n";
    f << "    <score-part id=\"P1\"><part-name>Guitar Chords</part-name></score-part>\n";
    f << "  </part-list>\n";
    f << "  <part id=\"P1\">\n";
    size_t chordIdx = 0;
    for (int m = 1; m <= measureCount; ++m) {
        f << "    <measure number=\"" << m << "\">\n";
        if (m == 1) {
            f << "      <attributes>\n";
            f << "        <divisions>1</divisions>\n";
            f << "        <key><fifths>0</fifths></key>\n";
            f << "        <time><beats>4</beats><beat-type>4</beat-type></time>\n";
            f << "        <clef><sign>G</sign><line>2</line></clef>\n";
            f << "      </attributes>\n";
            if (v.capo > 0) {
                f << "      <direction placement=\"above\">\n";
                f << "        <direction-type><words>Capo " << v.capo << "</words></direction-type>\n";
                f << "      </direction>\n";
            }
            f << "      <direction placement=\"above\">\n";
            f << "        <direction-type><words>Source: " << sanitizeXml(v.provider + " | " + v.sourceUrl) << "</words></direction-type>\n";
            f << "      </direction>\n";
        }
        for (int b = 0; b < beatsPerMeasure; ++b) {
            if (chordIdx < chords.size()) {
                const auto& pc = chords[chordIdx];
                f << "      <harmony>\n";
                f << "        <root>\n";
                f << "          <root-step>" << pc.root.step << "</root-step>\n";
                if (pc.root.alter != 0)
                    f << "          <root-alter>" << pc.root.alter << "</root-alter>\n";
                f << "        </root>\n";
                f << "        <kind text=\"" << sanitizeXml(pc.suffix)
                  << "\" use-symbols=\"no\">" << pc.kind << "</kind>\n";
                if (pc.bass) {
                    f << "        <bass><bass-step>" << pc.bass->step << "</bass-step>";
                    if (pc.bass->alter)
                        f << "<bass-alter>" << pc.bass->alter << "</bass-alter>";
                    f << "</bass>\n";
                }
                for (const auto& degree : pc.degrees) {
                    f << "        <degree print-object=\"no\"><degree-value>" << degree.value
                      << "</degree-value><degree-alter>" << degree.alter
                      << "</degree-alter><degree-type>" << degree.type << "</degree-type></degree>\n";
                }
                f << "      </harmony>\n";
            }
            f << "      <note>\n";
            f << "        <pitch><step>C</step><octave>4</octave></pitch>\n";
            f << "        <duration>1</duration>\n";
            f << "        <type>quarter</type>\n";
            f << "        <voice>1</voice>\n";
            f << "      </note>\n";
            ++chordIdx;
        }
        f << "    </measure>\n";
    }
    f << "  </part>\n";
    f << "</score-partwise>\n";
    f.flush();
    return static_cast<bool>(f);
}

bool exportToMusicXml(const TabVersion& version, const std::filesystem::path& path) {
    if (parseChords(version).empty()) return false;
    std::ofstream output(path, std::ios::binary);
    return output.is_open() && writeMusicXml(version, output);
}
} // namespace tabs
