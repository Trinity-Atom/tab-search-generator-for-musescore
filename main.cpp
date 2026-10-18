#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "providers.hpp"
#include "musicxml.hpp"

using namespace std;

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
            if (cin.eof()) return minVal;
            clearInput();
        }
        cout << "Entrada invalida. Intenta de nuevo.\n";
    }
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
        if (!line.chord.empty()) cout << "[" << line.chord << "] ";
        cout << line.lyric << "\n";
    }
    cout << "\n";
}

int main() {
    cout << "=== Buscador de tablaturas + exportador MusicXML (prototipo) ===\n";
    tabs::NativeHttpClient http;
    tabs::SearchService search(http);

    while (true) {
        cout << "\nEscribe una cancion para buscar (o 'salir'): ";
        string query;
        if (!getline(cin, query)) break;

        if (query == "salir" || query == "exit") {
            cout << "Hasta luego.\n";
            break;
        }
        if (query.find_first_not_of(" \t\r\n") == string::npos) continue;

        cout << "Buscando en LaCuerda, CifraClub y UltimateGuitar...\n";
        auto report = search.search(query);
        for (const auto& error : report.errors) cerr << "Aviso: " << error << "\n";
        auto& versions = report.versions;
        if (versions.empty()) {
            cout << (report.errors.empty() ? "No se encontraron resultados.\n"
                                          : "No hay resultados disponibles; revisa los avisos de proveedores.\n");
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

            auto& cached = versions[opt - 1];
            if (cached.lines.empty()) {
                try {
                    cout << "Cargando contenido publico de " << cached.provider << "...\n";
                    cached = search.load(cached);
                } catch (const tabs::ProviderError& error) {
                    cerr << "Error al cargar " << cached.provider << ": " << error.what()
                         << "\nFuente: " << cached.sourceUrl << "\n";
                    continue;
                }
            }
            const TabVersion& selected = cached;
            bool backToList = false;

            while (!backToList) {
                previewVersion(selected);

                cout << "1) Exportar a MusicXML\n";
                cout << "2) Volver al listado de versiones\n";
                cout << "3) Nueva busqueda\n";
                cout << "0) Salir\n";

                int action = askInt("Elige una opcion: ", 0, 3);

                if (action == 1) {
                    string basename = selected.title + "_" + selected.provider + ".musicxml";
                    // Sanitizar nombre de archivo básico
                    for (char& c : basename) {
                        if (static_cast<unsigned char>(c) < 32 || c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') {
                            c = '_';
                        }
                    }
                    std::error_code directoryError;
                    std::filesystem::create_directories("exports", directoryError);
                    if (directoryError) {
                        cerr << "Error al crear exports: " << directoryError.message() << "\n";
                        continue;
                    }
                    string filename = (std::filesystem::path("exports") / basename).string();

                    bool ok;
                    try {
                        ok = tabs::exportToMusicXml(selected, filename);
                    } catch (const tabs::ChordParseError& error) {
                        cerr << "Error al exportar: " << error.what() << "\n";
                        continue;
                    }
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
