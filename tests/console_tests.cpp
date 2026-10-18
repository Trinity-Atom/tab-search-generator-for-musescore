#include "musicxml.hpp"
#include "chords.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <random>

int main() {
    std::random_device random;
    const auto output = std::filesystem::temp_directory_path() /
        ("tab_search_export_test_" + std::to_string(random()) + "_" +
         std::to_string(random()) + ".musicxml");
    try {
        TabVersion version{1, "Test", "Tema & prueba", "Artista", "v1",
                           "https://example.org/?a=1&b=2", 2,
                           {{"C", "uno"}, {"G", "dos"}, {"Am", "fin"}, {"F", ""}, {"Dm7", ""}}};
        if (!tabs::exportToMusicXml(version, output))
            throw std::runtime_error("Fallo al exportar");
        std::ifstream file(output, std::ios::binary);
        std::string xml((std::istreambuf_iterator<char>(file)), {});
        std::ostringstream sharedOutput;
        if (!tabs::writeMusicXml(version, sharedOutput) || sharedOutput.str() != xml)
            throw std::runtime_error("Consola y GUI deben utilizar el mismo exportador");
        auto count = [&](const std::string& token) {
            size_t n = 0, pos = 0;
            while ((pos = xml.find(token, pos)) != std::string::npos) { ++n; pos += token.size(); }
            return n;
        };
        if (count("<harmony>") != 5 || count("<measure number=") != 2 ||
            count("<note>") != 8 || xml.find("Tema &amp; prueba") == std::string::npos ||
            xml.find("Capo 2") == std::string::npos ||
            xml.find("https://example.org/?a=1&amp;b=2") == std::string::npos)
            throw std::runtime_error("MusicXML no conserva acordes, compases o atribucion");
        version.lines.clear();
        if (tabs::exportToMusicXml(version, output))
            throw std::runtime_error("No inventar acordes al exportar contenido vacio");
        file.close();
        version.lines = {{"C7(b9)/E", ""}, {"NoValido", ""}};
        try {
            tabs::exportToMusicXml(version, output);
            throw std::runtime_error("Se esperaba rechazo del acorde invalido");
        } catch (const tabs::ChordParseError&) {}
        std::ifstream preserved(output, std::ios::binary);
        const std::string unchanged((std::istreambuf_iterator<char>(preserved)), {});
        if (unchanged != xml) throw std::runtime_error("El error debe conservar el archivo anterior");
        preserved.close();
        std::filesystem::remove(output);
        std::cout << "Pruebas de exportacion OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code cleanupError;
        std::filesystem::remove(output, cleanupError);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
