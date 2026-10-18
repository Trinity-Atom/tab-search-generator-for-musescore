#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace std;

// ---------- Modelo ----------
struct ChordLine {
    string chord;  // p.ej. "Am"
    string lyric;  // p.ej. "Hello darkness..."
};

struct TabVersion {
    int id{};
    string provider;      // LaCuerda / CifraClub / UltimateGuitar
    string title;
    string artist;
    string versionName;   // "Acoustic", "Original", etc.
    string sourceUrl;
    int capo{0};
    vector<ChordLine> lines;
};

// ---------- Utilidades ----------
static void clearInput() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

static int askInt(const string& prompt, int minVal, int maxVal) {
    while (true) {
        cout << prompt;
        int x;
        if (cin >> x) {
            clearInput();
            if (x >= minVal && x <= maxVal) return x;
        } else {
            clearInput();
        }
        cout << "Entrada invalida. Intenta de nuevo.\n";
    }
}

static string sanitizeXml(const string& s) {
    string out;
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

// Convierte un acorde tipo C, Am, F#, Bb, Dm7 a raiz + alter + tipo
// alter: 1 (#), -1 (b), 0 natural
struct ParsedChord {
    string root;     // A B C D E F G
    int alter{0};    // -1, 0, 1
    string kind;     // major, minor, dominant, major-seventh, etc (simplificado)
    string text;     // texto original
};

static ParsedChord parseChord(const string& chordText) {
    ParsedChord pc;
    pc.text = chordText;
    if (chordText.empty()) {
        pc.root = "C";
        pc.kind = "major";
        return pc;
    }

    // Primera letra raiz
    char r = chordText[0];
    if (r < 'A' || r > 'G') {
        pc.root = "C";
        pc.kind = "major";
        return pc;
    }
    pc.root = string(1, r);

    size_t i = 1;
    if (i < chordText.size()) {
        if (chordText[i] == '#') {
            pc.alter = 1;
            ++i;
        } else if (chordText[i] == 'b') {
            pc.alter = -1;
            ++i;
        }
    }

    string suffix = chordText.substr(i); // m, 7, maj7, m7...
    // Heurística simple:
    if (suffix.rfind("maj7", 0) == 0 || suffix.rfind("M7", 0) == 0) {
        pc.kind = "major-seventh";
    } else if (suffix.rfind("m7", 0) == 0) {
        pc.kind = "minor-seventh";
    } else if (!suffix.empty() && suffix[0] == 'm') {
        pc.kind = "minor";
    } else if (!suffix.empty() && suffix[0] == '7') {
        pc.kind = "dominant";
    } else {
        pc.kind = "major";
    }

    return pc;
}

// Exporta acordes (sin melodía) en MusicXML.
// Crea compases de 4/4 y coloca un acorde por negra con nota de "relleno" C4.
static bool exportToMusicXml(const TabVersion& v, const string& outputPath) {
    ofstream f(outputPath, ios::binary);
    if (!f.is_open()) return false;

    // Flatten de acordes
    vector<string> chords;
    for (const auto& line : v.lines) {
        if (!line.chord.empty()) chords.push_back(line.chord);
    }
    if (chords.empty()) chords.push_back("C");

    const int beatsPerMeasure = 4;
    int measureCount = static_cast<int>((chords.size() + beatsPerMeasure - 1) / beatsPerMeasure);

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

    int chordIdx = 0;
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
            string ch = (chordIdx < (int)chords.size()) ? chords[chordIdx] : "N.C.";
            ParsedChord pc = parseChord(ch);

            if (ch != "N.C.") {
                f << "      <harmony>\n";
                f << "        <root>\n";
                f << "          <root-step>" << pc.root << "</root-step>\n";
                if (pc.alter != 0) {
                    f << "          <root-alter>" << pc.alter << "</root-alter>\n";
                }
                f << "        </root>\n";
                f << "        <kind>" << pc.kind << "</kind>\n";
                f << "      </harmony>\n";
            }

            // Nota de relleno para que MuseScore dibuje duración/compás
            // (Luego puedes reemplazarla por patrón real o silencio)
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

    return true;
}

// ---------- "Proveedores" mock ----------
static vector<TabVersion> mockSearch(const string& query) {
    // En producción: aquí llamas adaptadores reales por proveedor.
    vector<TabVersion> all = {
        {
            1, "LaCuerda", query, "Artista Demo", "Version Acustica",
            "https://www.lacuerda.net/demo1", 2,
            {
                {"Am", "Hello darkness my old friend"},
                {"F",  "I've come to talk with you again"},
                {"C",  "Because a vision softly creeping"},
                {"G",  "Left its seeds while I was sleeping"},
                {"Am", "And the vision that was planted in my brain"},
                {"G",  "Still remains"},
                {"F",  "Within the sound of silence"}
            }
        },
        {
            2, "CifraClub", query, "Artista Demo", "Version Original",
            "https://www.cifraclub.com/demo2", 0,
            {
                {"C",  "When I find myself in times of trouble"},
                {"G",  "Mother Mary comes to me"},
                {"Am", "Speaking words of wisdom"},
                {"F",  "Let it be"}
            }
        },
        {
            3, "UltimateGuitar", query, "Artista Demo", "Simplificada",
            "https://www.ultimate-guitar.com/demo3", 1,
            {
                {"G",  "Country roads, take me home"},
                {"D",  "To the place I belong"},
                {"Em", "West Virginia"},
                {"C",  "Mountain mama"}
            }
        }
    };
    return all;
}

// ---------- UI ----------
static void printResults(const vector<TabVersion>& versions) {
    cout << "\n=== Resultados encontrados ===\n";
    for (size_t i = 0; i < versions.size(); ++i) {
        const auto& v = versions[i];
        cout << (i + 1) << ") "
             << v.title << " - " << v.artist
             << " | " << v.versionName
             << " | " << v.provider
             << " | capo: " << v.capo
             << "\n";
    }
    cout << "0) Nueva busqueda / salir\n";
}

static void previewVersion(const TabVersion& v) {
    cout << "\n=== Vista previa ===\n";
    cout << v.title << " - " << v.artist << "\n";
    cout << "Proveedor: " << v.provider << "\n";
    cout << "Version: " << v.versionName << "\n";
    cout << "URL: " << v.sourceUrl << "\n";
    cout << "Capo: " << v.capo << "\n\n";

    for (const auto& line : v.lines) {
        cout << "[" << line.chord << "] " << line.lyric << "\n";
    }
    cout << "\n";
}

int main() {
    cout << "=== Buscador de tablaturas + exportador MusicXML (prototipo) ===\n";

    while (true) {
        cout << "\nEscribe una cancion para buscar (o 'salir'): ";
        string query;
        getline(cin, query);

        if (query == "salir" || query == "exit") {
            cout << "Hasta luego.\n";
            break;
        }
        if (query.empty()) continue;

        vector<TabVersion> versions = mockSearch(query);
        if (versions.empty()) {
            cout << "No se encontraron resultados.\n";
            continue;
        }

        bool backToSearch = false;
        while (!backToSearch) {
            printResults(versions);
            int opt = askInt("Selecciona una version: ", 0, (int)versions.size());

            if (opt == 0) {
                backToSearch = true;
                continue;
            }

            TabVersion selected = versions[opt - 1];
            bool backToList = false;

            while (!backToList) {
                previewVersion(selected);

                cout << "1) Exportar a MusicXML\n";
                cout << "2) Volver al listado de versiones\n";
                cout << "3) Nueva busqueda\n";
                cout << "0) Salir\n";

                int action = askInt("Elige una opcion: ", 0, 3);

                if (action == 1) {
                    std::filesystem::create_directories("exports");
                    string filename = "exports/" + selected.title + "_" + selected.provider + ".musicxml";
                    // Sanitizar nombre de archivo básico
                    for (char& c : filename) {
                        if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') {
                            c = '_';
                        }
                    }

                    bool ok = exportToMusicXml(selected, filename);
                    if (ok) {
                        cout << "Exportacion exitosa: " << filename << "\n";
                        cout << "Abre este archivo en MuseScore para editar/visualizar.\n";
                    } else {
                        cout << "Error al exportar el archivo.\n";
                    }
                } else if (action == 2) {
                    backToList = true;
                } else if (action == 3) {
                    backToList = true;
                    backToSearch = true;
                } else if (action == 0) {
                    cout << "Hasta luego.\n";
                    return 0;
                }
            }
        }
    }

    return 0;
}
