#include "providers.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expectError(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const tabs::ProviderError&) {
        return;
    }
    throw std::runtime_error("Se esperaba ProviderError");
}

class FakeHttp final : public tabs::HttpClient {
public:
    std::map<std::string, std::string> pages;
    std::vector<std::string> requests;
    std::string get(const std::string& url) override {
        requests.push_back(url);
        auto found = pages.find(url);
        if (found == pages.end()) throw tabs::ProviderError("HTTP 403 (prueba)");
        return found->second;
    }
};

std::string store(const std::string& data) {
    std::string escaped;
    for (char c : data) {
        if (c == '&') escaped += "&amp;";
        else if (c == '\'') escaped += "&#39;";
        else escaped += c;
    }
    return "<div class='js-store' data-content='" + escaped + "'></div>";
}

const std::string lcUrl = "https://acordes.lacuerda.net/artista/tema.shtml";
const std::string ccUrl = "https://www.cifraclub.com/artista/tema/";
const std::string ugUrl = "https://tabs.ultimate-guitar.com/tab/artista/tema-chords-123";

void testAdapters() {
    FakeHttp http;
    auto providers = tabs::makeProviders(http);
    const std::string query = "luz \xC3\xB1 &";
    const std::string encoded = "luz%20%C3%B1%20%26";
    http.pages["https://acordes.lacuerda.net/busca.php?exp=" + encoded] =
        "<ul id=i_main><li><a href='/artista/tema.shtml'>Tema &amp; prueba</a>"
        "<a href='/artista/tema.shtml'>Duplicado</a><a href='/artista/'>Artista</a>"
        "<a href='https://example.org/artista/tema'>Externo</a></li></ul>";
    http.pages["https://solr.sscdn.co/cc/h2/?q=" + encoded] =
        R"(({"response":{"docs":[{"t":"1","a":"Artista"},{"t":"2","m":"Tema & prueba","a":"Artista","d":"artista","u":"tema"},{"t":"2","m":"Tema","a":"Artista","d":"artista","u":"tema"}]}}))";
    http.pages["https://www.ultimate-guitar.com/search.php?search_type=title&value=" + encoded] =
        store(R"({"store":{"page":{"data":{"results":[
          {"type":"Chords","tab_access_type":"public","version":2,"song_name":"Tema & prueba","artist_name":"Artista","tab_url":"https://tabs.ultimate-guitar.com/tab/artista/tema-chords-123"},
          {"type":"Tabs"},
          {"type":"Chords","tab_access_type":"private"},
          {"marketing_type":"TabPro"}
        ]}}}})");

    tabs::SearchService service(http);
    auto report = service.search(query);
    require(report.errors.empty(), "La busqueda valida no debe fallar");
    require(report.versions.size() == 3, "Tres resultados unicos de canciones");
    require(http.requests.size() == 3, "Buscar no descarga contenido");
    for (size_t i = 0; i < report.versions.size(); ++i) {
        const auto& v = report.versions[i];
        require(v.id == static_cast<int>(i) + 1, "IDs globales consecutivos");
        require(v.title == "Tema & prueba", "Entidades HTML/JSON decodificadas");
        require(v.lines.empty(), "Carga diferida");
    }
    require(report.versions[2].versionName == "Chords v2", "Version real UG");

    http.pages[lcUrl] =
        "<pre id=tCode></pre><div id=t_body><pre>Capo: 2\n"
        "<a>C</a>   <a>G</a>\nuno dos\nAm\nfin\nF</pre></div>";
    http.pages[ccUrl] =
        "<span id='cifra_capo'>3</span><div class='cifra_cnt g-fix'>"
        "<pre><b>C</b>   <b>G</b><br>uno dos<br><b>Am</b><br>fin<br><b>F</b></pre></div>";
    http.pages[ugUrl] = store(
        R"({"store":{"page":{"data":{"tab":{"type":"Chords","tab_access_type":"public"},"tab_view":{"wiki_tab":{"content":"[tab][ch]C[/ch]   [ch]G[/ch]\r\nuno dos\r\n[ch]Am[/ch]\r\nfin\r\n[ch]F[/ch][/tab]"},"meta":{"capo":4}}}}}})");
    for (size_t i = 0; i < report.versions.size(); ++i) {
        const auto loaded = service.load(report.versions[i]);
        std::vector<std::string> chords;
        for (const auto& line : loaded.lines)
            if (!line.chord.empty()) chords.push_back(line.chord);
        require(chords == std::vector<std::string>({"C", "G", "Am", "F"}),
                "Todos los acordes, incluido el ultimo instrumental");
        require(loaded.capo == static_cast<int>(i) + 2, "Capo por fuente");
        const auto first = std::find_if(loaded.lines.begin(), loaded.lines.end(),
            [](const ChordLine& line) { return line.chord == "C"; });
        require(first != loaded.lines.end() && first->lyric == "uno", "Alineacion letra/acorde");
        require(loaded.sourceUrl == report.versions[i].sourceUrl, "Conservar atribucion");
        require(report.versions[i].lines.empty(), "No mutar metadatos en caso de error");
    }

    http.pages[lcUrl] = "<pre id=tCode>Texto sintetico sin acordes</pre>";
    http.pages[ccUrl] = "<html>Acceso restringido</html>";
    http.pages[ugUrl] = store(
        R"({"store":{"page":{"data":{"tab":{"type":"Chords","tab_access_type":"private"}}}}})");
    for (const auto& v : report.versions) expectError([&] { service.load(v); });
    http.pages[lcUrl] =
        "<meta charset='utf-8'><div id=t_body><pre>C G\n"
        "\xC3\xA1 \xC3\xA9\nAm\tF\nuno     dos</pre></div>";
    auto unicode = service.load(report.versions.front());
    require(unicode.lines.size() == 4 &&
            unicode.lines[0].lyric == "\xC3\xA1" && unicode.lines[1].lyric == "\xC3\xA9",
            "No dividir caracteres UTF-8 al alinear acordes");
    require(unicode.lines[3].chord == "F" && unicode.lines[3].lyric == "dos",
            "Tabulaciones alineadas a ocho columnas");
    http.pages[lcUrl] =
        "<pre id=tCode></pre><div id=t_body><pre>Intro:\n\n<div></div>"
        "<a>Bm</a>-<a>G</a>-<a>D</a>-<a>A</a> (x2)\n\n<div></div>"
        "<a>Bm</a>  <a>G</a>  (<a>D</a>-<a>A</a>)\nTexto de prueba\n</pre></div>";
    auto progression = service.load(report.versions.front());
    std::vector<std::string> progressionChords;
    for (const auto& line : progression.lines)
        if (!line.chord.empty()) progressionChords.push_back(line.chord);
    require(progressionChords == std::vector<std::string>({"Bm", "G", "D", "A", "Bm", "G", "D", "A"}),
            "Bloques HTML, guiones, parentesis y repeticiones de LaCuerda");
    http.pages[lcUrl] = "<meta charset='utf-8'><div id=t_body><pre>"
        "C7(b9) D/F# Csus4 Cadd9 Bdim7 Bm7b5 Caug\n"
        "Texto sintetico\n(C7(b9)-D/F#) (x2)\n"
        "C\xE2\x99\xAFm  G\nfin</pre></div>";
    const auto complex = service.load(report.versions.front());
    std::vector<std::string> complexChords;
    for (const auto& line : complex.lines)
        if (!line.chord.empty()) complexChords.push_back(line.chord);
    require(complexChords == std::vector<std::string>({
        "C7(b9)", "D/F#", "Csus4", "Cadd9", "Bdim7", "Bm7b5", "Caug",
        "C7(b9)", "D/F#", "C\xE2\x99\xAFm", "G"}),
        "Conservar modificadores internos, inversiones y simbolos originales");
    http.pages[ccUrl] = "<div class=cifra_cnt><pre><b>Csus4</b> <b>Cadd9</b> "
        "<b>Bdim7</b> <b>D/F#</b> <b>C7(b9)</b>\nTexto sintetico</pre></div>";
    http.pages[ugUrl] = store(
        R"({"store":{"page":{"data":{"tab":{"type":"Chords","tab_access_type":"public"},"tab_view":{"wiki_tab":{"content":"[ch]Csus4[/ch] [ch]Cadd9[/ch] [ch]Bdim7[/ch] [ch]D/F#[/ch] [ch]C7(b9)[/ch]\nTexto sintetico"},"meta":{"capo":0}}}}}})");
    for (size_t index : {size_t{1}, size_t{2}}) {
        const auto source = service.load(report.versions[index]);
        std::vector<std::string> sourceChords;
        for (const auto& line : source.lines)
            if (!line.chord.empty()) sourceChords.push_back(line.chord);
        require(sourceChords == std::vector<std::string>({"Csus4", "Cadd9", "Bdim7", "D/F#", "C7(b9)"}),
                "CifraClub y UltimateGuitar usan el mismo parser semantico");
    }
    http.pages[lcUrl] = "<meta charset='utf-8'><pre id=tCode>C\xE2\x99\xAFm  G\nuno  dos</pre>";
    const auto unicodeChords = service.load(report.versions.front());
    require(unicodeChords.lines[0].lyric == "uno" && unicodeChords.lines[1].lyric == "dos",
            "La anchura de la alteracion Unicode no desplaza la letra");
    for (const auto* content : {"C Csus3 G", "C C7(b9 G", "C C7b9) G"}) {
        http.pages[lcUrl] = std::string("<pre id=tCode>") + content + "</pre>";
        expectError([&] { service.load(report.versions.front()); });
    }
    auto invalid = report.versions[0];
    invalid.sourceUrl = "https://example.org/tema";
    const auto before = http.requests.size();
    expectError([&] { service.load(invalid); });
    require(http.requests.size() == before, "Rechazar URL externa antes de HTTP");
    invalid.provider = "Desconocido";
    expectError([&] { service.load(invalid); });
    expectError([&] { service.search(" \t"); });
}

void testEmptyAndPartialFailure() {
    FakeHttp http;
    http.pages["https://acordes.lacuerda.net/busca.php?exp=x"] =
        "<div class=s_res>0 resultados</div><ul id=i_main></ul>";
    http.pages["https://solr.sscdn.co/cc/h2/?q=x"] = R"({"response":{"docs":[]}})";
    http.pages["https://www.ultimate-guitar.com/search.php?search_type=title&value=x"] =
        store(R"({"store":{"page":{"data":{"results":[]}}}})");
    tabs::SearchService service(http);
    auto report = service.search("x");
    require(report.versions.empty() && report.errors.empty(), "Cero resultados legitimo");
    http.pages.erase("https://solr.sscdn.co/cc/h2/?q=x");
    report = service.search("x");
    require(report.errors.size() == 1 && report.errors[0].find("CifraClub:") == 0,
            "Un bloqueo no afecta a otros proveedores");
    http.pages["https://www.ultimate-guitar.com/search.php?search_type=title&value=x"] =
        store(R"({"store":{"page":{"data":{"results":[{"type":"Chords","version":1,"song_name":"Tema","artist_name":"Artista","tab_url":"https://tabs.ultimate-guitar.com/tab/artista/tema-chords-123"}]}}}})");
    report = service.search("x");
    require(report.errors.size() == 1 && report.versions.size() == 1 &&
            report.versions.front().provider == "UltimateGuitar" && report.versions.front().id == 1,
            "Los resultados reales sobreviven al fallo de otra fuente");
    http.pages["https://acordes.lacuerda.net/busca.php?exp=x"] = "<ul id=i_main>";
    http.pages["https://www.ultimate-guitar.com/search.php?search_type=title&value=x"] =
        store("{invalid");
    report = service.search("x");
    require(report.errors.size() == 3, "HTML incompleto y JSON roto son errores, no vacios");
    http.pages["https://solr.sscdn.co/cc/h2/?q=x"] =
        R"({"response":{"docs":[{"t":"2","m":123}]}})";
    report = service.search("x");
    require(report.errors.size() == 3, "Cambios de esquema notificados");
}

void testTransportValidation() {
    tabs::NativeHttpClient http;
    for (const auto* url : {"http://acordes.lacuerda.net/", "https://example.org/",
                            "https://www.cifraclub.com@localhost/", "https://localhost/"})
        expectError([&] { http.get(url); });
}

void liveSearch(const std::string& source = {}, const std::string& url = {}) {
    tabs::NativeHttpClient http;
    auto providers = tabs::makeProviders(http);
    if (!source.empty()) {
        for (auto& provider : providers) {
            if (provider->name() != source) continue;
            TabVersion metadata{0, source, "Diagnostico", "", "", url, 0, {}};
            auto loaded = provider->load(metadata);
            std::cout << source << ": carga OK, " << loaded.lines.size() << " segmentos\n";
            return;
        }
        throw tabs::ProviderError("Proveedor desconocido: " + source);
    }
    for (auto& provider : providers) {
        try {
            auto versions = provider->search("luz");
            std::cout << provider->name() << ": " << versions.size() << " resultados\n";
            if (!versions.empty()) {
                try {
                    auto version = provider->load(versions.front());
                    std::cout << "  Carga OK: " << version.lines.size() << " segmentos\n";
                } catch (const tabs::ProviderError& error) {
                    std::cout << "  Carga no disponible: " << error.what() << '\n';
                }
            }
        } catch (const tabs::ProviderError& error) {
            std::cout << provider->name() << ": " << error.what() << '\n';
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if ((argc == 2 || argc == 4) && std::string(argv[1]) == "--live") {
            liveSearch(argc == 4 ? argv[2] : "", argc == 4 ? argv[3] : "");
            return 0;
        }
        testAdapters();
        testEmptyAndPartialFailure();
        testTransportValidation();
        std::cout << "Pruebas de proveedores OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
