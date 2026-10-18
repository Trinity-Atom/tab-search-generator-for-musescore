#include "chords.hpp"
#include "musicxml.hpp"

#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <functional>
#include <iostream>
#include <sstream>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void invalid(const std::function<void()>& action) {
    try { action(); }
    catch (const tabs::ChordParseError&) { return; }
    throw std::runtime_error("Se esperaba un error de acorde");
}

void harmony(const std::string& symbol, const std::string& kind,
             const std::string& condition = "true()") {
    const auto parsed = tabs::parseChord(symbol);
    require(parsed.kind == kind, "Tipo incorrecto: " + symbol);
    require(parsed.original == symbol, "Simbolo original perdido");
    TabVersion version;
    version.lines = {{symbol, ""}};
    std::ostringstream output;
    require(tabs::writeMusicXml(version, output), "No se genero XML");
    const auto xml = output.str();
    auto* doc = xmlReadMemory(xml.data(), static_cast<int>(xml.size()), nullptr, nullptr,
                              XML_PARSE_NONET | XML_PARSE_NO_XXE);
    require(doc != nullptr, "XML mal formado");
    auto* context = xmlXPathNewContext(doc);
    const auto query = "boolean(//harmony[kind='" + kind + "' and " + condition + "])";
    auto* result = xmlXPathEvalExpression(BAD_CAST query.c_str(), context);
    const bool ok = result && xmlXPathCastToBoolean(result);
    if (result) xmlXPathFreeObject(result);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);
    require(ok, "Semantica MusicXML incorrecta: " + symbol);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--export-example") {
            TabVersion example;
            example.title = "Prueba de acordes";
            example.versionName = "Fmaj7b5 - Bbmaj7 - D7";
            example.lines = {{"Fmaj7b5", ""}, {"Bbmaj7", ""}, {"D7", ""}};
            require(tabs::exportToMusicXml(example, argv[2]), "No se pudo exportar la prueba");
        } else if (argc != 1) {
            throw std::runtime_error("Uso: chord_tests [--export-example ruta.musicxml]");
        }
        harmony("C", "major", "root/root-step='C' and not(degree)");
        harmony("Am", "minor");
        harmony("F#7", "dominant", "root/root-alter=1");
        harmony("Bbmaj7", "major-seventh", "root/root-alter=-1");
        harmony("Fmaj7b5", "major-seventh",
                "root/root-step='F' and not(root/root-alter) and kind/@text='maj7b5' and "
                "degree[degree-value=5 and degree-alter=-1 and degree-type='alter']");
        harmony("Bbmaj7", "major-seventh",
                "root/root-step='B' and root/root-alter=-1 and kind/@text='maj7' and not(degree)");
        harmony("D7", "dominant",
                "root/root-step='D' and not(root/root-alter) and kind/@text='7' and not(degree)");
        harmony("Dm7", "minor-seventh");
        harmony("Csus4", "suspended-fourth");
        harmony("Dsus2", "suspended-second");
        harmony("Bdim", "diminished");
        harmony("Bdim7", "diminished-seventh");
        harmony("Bm7b5", "half-diminished", "not(degree)");
        harmony("Caug", "augmented");
        harmony("Cadd9", "major",
                "degree[degree-value=9 and degree-alter=0 and degree-type='add'] and count(degree)=1");
        harmony("C9", "dominant-ninth", "not(degree)");
        harmony("G7b9", "dominant", "degree[degree-value=9 and degree-alter=-1 and degree-type='add']");
        harmony("C9#9", "dominant-ninth", "degree[degree-value=9 and degree-alter=1 and degree-type='alter']");
        harmony("D/F#", "major", "bass[bass-step='F' and bass-alter=1]");
        harmony("C7(b9)/E", "dominant",
                "bass/bass-step='E' and kind/@text='7(b9)' and degree/degree-value=9");
        harmony("C7(b9,#11)", "dominant", "count(degree)=2");
        harmony("C7sus4", "suspended-fourth",
                "degree[degree-value=7 and degree-alter=-1 and degree-type='add']");
        harmony("CmMaj7", "major-minor");
        harmony("Cm6", "minor-sixth");
        harmony("C13", "dominant-13th");
        harmony("Cm11", "minor-11th");
        harmony("C5", "power");
        harmony("DOsus4", "suspended-fourth");
        harmony("C\xE2\x99\xAFm", "minor", "root/root-alter=1");
        harmony("C\xC2\xB0", "diminished");
        harmony("C+", "augmented");
        harmony("B\xC3\xB8", "half-diminished");
        require(tabs::normalizeChord("REm7/FA#") == "Dm7/F#", "Normalizar raiz y bajo");
        require(tabs::normalizeChord("CM7") == "Cmaj7", "Alias M7");
        require(tabs::normalizeChord("Cmin7") == "Cm7", "Alias min");
        for (const auto* symbol : {"", "H7", "Cwat", "C14", "C999", "C7foo", "C7/",
             "C/H", "C/E/G", "C##", "Csus", "Csus3", "Cadd", "Cadd8",
             "C7(b9", "C7b9)", "C7()", "C7((b9))", "C7(b9,)", "C7(,b9)",
             "C7(b9,#9)", "Cadd3", "C7add7", "C G", "C7<script>"})
            invalid([&] { tabs::parseChord(symbol); });
        invalid([] { tabs::parseChord("C7sus4add7"); });
        TabVersion bad;
        bad.lines = {{"C", ""}, {"H7", ""}};
        std::ostringstream output;
        invalid([&] { tabs::writeMusicXml(bad, output); });
        require(output.str().empty(), "No escribir XML parcial en un error");
        std::cout << "Pruebas de parsing y armonia MusicXML OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
