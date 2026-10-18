#include "providers.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <regex>
#include <set>
#include <sstream>
#include <utility>

namespace tabs {
namespace {
using Json = nlohmann::json;

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string encode(const std::string& text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            result += static_cast<char>(c);
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}

class Html {
public:
    explicit Html(const std::string& body)
        : doc_(htmlReadMemory(body.data(), static_cast<int>(body.size()), nullptr, nullptr,
                             HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING),
               xmlFreeDoc) {
        if (!doc_) throw ProviderError("HTML no valido");
    }

    std::vector<xmlNode*> nodes(const char* expression) const {
        std::unique_ptr<xmlXPathContext, decltype(&xmlXPathFreeContext)>
            context(xmlXPathNewContext(doc_.get()), xmlXPathFreeContext);
        if (!context) throw ProviderError("No se pudo crear el parser XPath");
        std::unique_ptr<xmlXPathObject, decltype(&xmlXPathFreeObject)>
            result(xmlXPathEvalExpression(BAD_CAST expression, context.get()), xmlXPathFreeObject);
        if (!result) throw ProviderError("No se pudo evaluar el selector HTML");
        std::vector<xmlNode*> out;
        if (result->nodesetval) {
            for (int i = 0; i < result->nodesetval->nodeNr; ++i)
                out.push_back(result->nodesetval->nodeTab[i]);
        }
        return out;
    }

    static std::string attr(xmlNode* node, const char* key) {
        xmlChar* value = xmlGetProp(node, BAD_CAST key);
        if (!value) return {};
        std::string result(reinterpret_cast<const char*>(value));
        xmlFree(value);
        return result;
    }

    static std::string text(xmlNode* node) {
        xmlChar* value = xmlNodeGetContent(node);
        if (!value) return {};
        std::string result(reinterpret_cast<const char*>(value));
        xmlFree(value);
        return result;
    }

    static std::string preText(xmlNode* node) {
        std::string result;
        for (auto* child = node->children; child; child = child->next) {
            if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE)
                result += text(child);
            else if (child->type == XML_ELEMENT_NODE) {
                const std::string tag(reinterpret_cast<const char*>(child->name));
                if (tag == "br") result += '\n';
                else if (tag != "script" && tag != "style") result += preText(child);
            }
        }
        return result;
    }

private:
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc_;
};

Json json(const std::string& text) {
    try {
        return Json::parse(text);
    } catch (const Json::exception& error) {
        throw ProviderError(std::string("JSON del proveedor no valido: ") + error.what());
    }
}

std::string requiredString(const Json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || trim(it->get<std::string>()).empty())
        throw ProviderError(std::string("Falta el campo de texto: ") + key);
    return it->get<std::string>();
}

const Json& field(const Json& object, const char* key) {
    auto it = object.find(key);
    if (it == object.end()) throw ProviderError(std::string("Falta el campo: ") + key);
    return *it;
}

int integer(const Json& value, const std::string& label, int maximum) {
    if (!value.is_number_integer()) throw ProviderError("Entero no valido: " + label);
    if (value < 0 || value > maximum) throw ProviderError("Entero fuera de rango: " + label);
    return value.get<int>();
}

size_t columnOffset(const std::string& text, size_t column) {
    size_t offset = 0;
    while (offset < text.size() && column > 0) {
        ++offset;
        while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80)
            ++offset;
        --column;
    }
    return offset;
}

std::string expandTabs(const std::string& text) {
    std::string result;
    size_t column = 0;
    for (unsigned char c : text) {
        if (c == '\t') {
            const auto spaces = 8 - column % 8;
            result.append(spaces, ' ');
            column += spaces;
        } else {
            result += static_cast<char>(c);
            if ((c & 0xC0) != 0x80) ++column;
        }
    }
    return result;
}

std::vector<ChordLine> parseLines(std::string text) {
    for (const auto* marker : {"[ch]", "[/ch]", "[tab]", "[/tab]"}) {
        size_t pos = 0;
        while ((pos = text.find(marker, pos)) != std::string::npos)
            text.erase(pos, std::char_traits<char>::length(marker));
    }
    struct Positioned { size_t column; std::string value; };
    std::vector<Positioned> pending;
    std::vector<ChordLine> result;
    auto flush = [&](const std::string& lyric) {
        if (pending.empty()) {
            if (!trim(lyric).empty()) result.push_back({{}, lyric});
            return;
        }
        if (pending.front().column > 0) {
            auto prefix = trim(lyric.substr(0, columnOffset(lyric, pending.front().column)));
            if (!prefix.empty()) result.push_back({{}, prefix});
        }
        for (size_t i = 0; i < pending.size(); ++i) {
            const auto start = columnOffset(lyric, pending[i].column);
            const auto end = i + 1 < pending.size()
                ? columnOffset(lyric, pending[i + 1].column) : lyric.size();
            result.push_back({pending[i].value, trim(lyric.substr(start, end - start))});
        }
        pending.clear();
    };
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = expandTabs(line);
        std::vector<Positioned> found;
        std::vector<std::string> unsupported;
        bool onlyChords = true;
        int groups = 0;
        static const std::regex repeat(R"(^(?:[xX][0-9]+|[0-9]+[xX])$)");
        static const std::regex chordLike(
            R"(^(?:[A-G]|DO|RE|MI|FA|SOL|LA|SI)[#b]?(?:[0-9(]|sus|add|dim|aug|maj|min|m[0-9(]).*$)");
        const std::string separators = " \t|-";
        for (size_t pos = 0; pos < line.size();) {
            if (separators.find(line[pos]) != std::string::npos) { ++pos; continue; }
            auto end = line.find_first_of(separators, pos);
            auto token = line.substr(pos, end == std::string::npos ? end : end - pos);
            // A leading parenthesis wraps a progression; chord modifiers follow the root.
            if (!token.empty() && token.front() == '(') { ++groups; ++pos; continue; }
            while (groups > 0 && !token.empty() && token.back() == ')' &&
                   std::count(token.begin(), token.end(), ')') > std::count(token.begin(), token.end(), '(')) {
                token.pop_back();
                --groups;
            }
            const auto parsed = normalizeChord(token);
            if (!parsed.empty()) {
                size_t column = 0;
                for (size_t offset = 0; offset < pos; ++offset)
                    if ((static_cast<unsigned char>(line[offset]) & 0xC0) != 0x80) ++column;
                found.push_back({column, token});
            }
            else if (std::regex_match(token, chordLike)) unsupported.push_back(token);
            else if (!std::regex_match(token, repeat)) { onlyChords = false; break; }
            pos = end == std::string::npos ? line.size() : end;
        }
        if (onlyChords && (!found.empty() || !unsupported.empty())) {
            if (!unsupported.empty())
                throw ProviderError("Acorde no compatible en la fuente: " + unsupported.front());
            if (groups) throw ProviderError("Parentesis incompletos en la linea de acordes");
            flush({});
            pending = std::move(found);
        } else {
            flush(line);
        }
    }
    flush({});
    if (std::none_of(result.begin(), result.end(),
                     [](const ChordLine& line) { return !line.chord.empty(); }))
        throw ProviderError("No se encontraron acordes compatibles en el contenido publico");
    return result;
}

int capoFromText(const std::string& text) {
    static const std::regex pattern(
        R"((?:capo|capotraste|cejilla)\s*(?::|en|no|na|on|at)?\s*([0-9]{1,2})\b)",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return 0;
    int capo = std::stoi(match[1]);
    if (capo > 24) throw ProviderError("Capo fuera de rango");
    return capo;
}

class Source : public Provider {
public:
    Source(HttpClient& http, std::string name) : http_(http), name_(std::move(name)) {}
    std::string name() const override { return name_; }
protected:
    HttpClient& http_;
    TabVersion version(std::string title, std::string artist,
                       std::string label, std::string url) const {
        return {0, name_, std::move(title), std::move(artist),
                std::move(label), std::move(url), 0, {}};
    }
    void validate(const TabVersion& v, const std::regex& urlPattern) const {
        if (v.provider != name_ || !std::regex_match(v.sourceUrl, urlPattern))
            throw ProviderError("La version no pertenece a " + name_);
    }
private:
    std::string name_;
};

class LaCuerda final : public Source {
public:
    explicit LaCuerda(HttpClient& http) : Source(http, "LaCuerda") {}

    std::vector<TabVersion> search(const std::string& query) override {
        Html doc(http_.get("https://acordes.lacuerda.net/busca.php?exp=" + encode(query)));
        std::vector<TabVersion> result;
        std::set<std::string> seen;
        auto links = doc.nodes("//*[@id='i_main']//a[@href]");
        for (auto* link : links) {
            std::string url = Html::attr(link, "href");
            if (url.rfind("//acordes.lacuerda.net/", 0) == 0) url = "https:" + url;
            else if (!url.empty() && url.front() == '/') url = "https://acordes.lacuerda.net" + url;
            else if (url.find("://") == std::string::npos) url = "https://acordes.lacuerda.net/" + url;
            std::smatch match;
            if (!std::regex_match(url, match, songUrl()) || !seen.insert(url).second) continue;
            std::string artist = match[1];
            std::replace(artist.begin(), artist.end(), '_', ' ');
            auto title = trim(Html::text(link));
            if (title.empty()) throw ProviderError("Resultado LaCuerda sin titulo");
            result.push_back(version(title, artist, "Acordes", url));
        }
        if (result.empty()) {
            auto summaries = doc.nodes("//div[contains(concat(' ',normalize-space(@class),' '),' s_res ')]");
            static const std::regex empty(R"(\b0 resultados\b)");
            if (summaries.empty() || !std::regex_search(Html::text(summaries.front()), empty))
                throw ProviderError("Resultados ausentes o HTML modificado/incompleto");
        }
        return result;
    }

    TabVersion load(const TabVersion& selected) override {
        validate(selected, songUrl());
        Html doc(http_.get(selected.sourceUrl));
        auto pre = doc.nodes("//*[@id='t_body']//pre");
        if (pre.empty()) pre = doc.nodes("//pre[@id='tCode']");
        if (pre.empty()) throw ProviderError("Falta el contenido publico t_body/pre");
        auto result = selected;
        const auto text = Html::preText(pre.front());
        result.lines = parseLines(text);
        result.capo = capoFromText(text);
        return result;
    }
private:
    static const std::regex& songUrl() {
        static const std::regex pattern(
            R"(^https://acordes\.lacuerda\.net/([a-z0-9_]+)/([a-z0-9_-]+)(?:\.shtml)?$)");
        return pattern;
    }
};

class CifraClub final : public Source {
public:
    explicit CifraClub(HttpClient& http) : Source(http, "CifraClub") {}

    std::vector<TabVersion> search(const std::string& query) override {
        auto body = trim(http_.get("https://solr.sscdn.co/cc/h2/?q=" + encode(query)));
        // The public search endpoint wraps its JSON in parentheses, not a callback.
        if (body.size() >= 2 && body.front() == '(' && body.back() == ')')
            body = body.substr(1, body.size() - 2);
        const auto data = json(body);
        const auto& response = field(data, "response");
        const auto& docs = field(response, "docs");
        if (!docs.is_array()) throw ProviderError("docs no es una lista");
        std::vector<TabVersion> result;
        std::set<std::string> seen;
        static const std::regex slug(R"(^[a-z0-9][a-z0-9-]*$)");
        for (const auto& entry : docs) {
            if (requiredString(entry, "t") != "2") continue; // Artist hits are not songs.
            const auto artist = requiredString(entry, "d");
            const auto song = requiredString(entry, "u");
            if (!std::regex_match(artist, slug) || !std::regex_match(song, slug))
                throw ProviderError("Ruta de cancion no valida");
            const auto url = "https://www.cifraclub.com/" + artist + "/" + song + "/";
            if (seen.insert(url).second)
                result.push_back(version(requiredString(entry, "m"), requiredString(entry, "a"),
                                         "Acordes", url));
        }
        return result;
    }

    TabVersion load(const TabVersion& selected) override {
        static const std::regex url(R"(^https://www\.cifraclub\.com/[a-z0-9-]+/[a-z0-9-]+/$)");
        validate(selected, url);
        Html doc(http_.get(selected.sourceUrl));
        auto pre = doc.nodes(
            "//*[contains(concat(' ',normalize-space(@class),' '),' cifra_cnt ')]//pre");
        if (pre.empty()) throw ProviderError("Falta el contenido publico cifra_cnt/pre");
        auto result = selected;
        result.lines = parseLines(Html::preText(pre.front()));
        auto capo = doc.nodes("//*[@id='cifra_capo' or @id='cifra_capo-value']");
        result.capo = capo.empty() ? 0 : capoFromText("Capo " + Html::text(capo.front()));
        return result;
    }
};

Json ugData(const std::string& body) {
    Html doc(body);
    auto store = doc.nodes(
        "//*[contains(concat(' ',normalize-space(@class),' '),' js-store ') and @data-content]");
    if (store.empty()) throw ProviderError("Falta js-store/data-content");
    auto data = json(Html::attr(store.front(), "data-content"));
    return field(field(field(data, "store"), "page"), "data");
}

class UltimateGuitar final : public Source {
public:
    explicit UltimateGuitar(HttpClient& http) : Source(http, "UltimateGuitar") {}

    std::vector<TabVersion> search(const std::string& query) override {
        const auto data = ugData(http_.get(
            "https://www.ultimate-guitar.com/search.php?search_type=title&value=" + encode(query)));
        const auto& entries = field(data, "results");
        if (!entries.is_array()) throw ProviderError("results no es una lista");
        std::vector<TabVersion> result;
        std::set<std::string> seen;
        for (const auto& entry : entries) {
            auto type = entry.find("type");
            if (type == entry.end() || !type->is_string() || *type != "Chords") continue;
            auto access = entry.find("tab_access_type");
            if (access != entry.end() && *access != "public") continue;
            const auto url = requiredString(entry, "tab_url");
            if (!std::regex_match(url, songUrl())) continue; // Exclude Pro and external links.
            auto number = integer(field(entry, "version"), "version", 1000000);
            if (seen.insert(url).second)
                result.push_back(version(requiredString(entry, "song_name"),
                                         requiredString(entry, "artist_name"),
                                         "Chords v" + std::to_string(number), url));
        }
        return result;
    }

    TabVersion load(const TabVersion& selected) override {
        validate(selected, songUrl());
        const auto data = ugData(http_.get(selected.sourceUrl));
        const auto& tab = field(data, "tab");
        if (requiredString(tab, "type") != "Chords")
            throw ProviderError("La version no es de acordes");
        auto access = tab.find("tab_access_type");
        if (access != tab.end() && *access != "public")
            throw ProviderError("Contenido no publico");
        const auto& view = field(data, "tab_view");
        auto result = selected;
        result.lines = parseLines(requiredString(field(view, "wiki_tab"), "content"));
        const auto& meta = field(view, "meta");
        auto capo = meta.find("capo");
        result.capo = capo == meta.end() || capo->is_null() ? 0 : integer(*capo, "capo", 24);
        return result;
    }
private:
    static const std::regex& songUrl() {
        static const std::regex pattern(
            R"(^https://tabs\.ultimate-guitar\.com/tab/[a-z0-9_-]+/[a-z0-9_-]+-chords-[0-9]+$)");
        return pattern;
    }
};

} // namespace

std::vector<std::unique_ptr<Provider>> makeProviders(HttpClient& http) {
    std::vector<std::unique_ptr<Provider>> result;
    result.push_back(std::make_unique<LaCuerda>(http));
    result.push_back(std::make_unique<CifraClub>(http));
    result.push_back(std::make_unique<UltimateGuitar>(http));
    return result;
}

SearchService::SearchService(HttpClient& http) : providers_(makeProviders(http)) {}

SearchReport SearchService::search(const std::string& query) {
    if (trim(query).empty()) throw ProviderError("La busqueda no puede estar vacia");
    SearchReport report;
    for (auto& provider : providers_) {
        try {
            auto found = provider->search(query);
            for (auto& version : found) {
                version.id = static_cast<int>(report.versions.size()) + 1;
                report.versions.push_back(std::move(version));
            }
        } catch (const ProviderError& error) {
            report.errors.push_back(provider->name() + ": " + error.what());
        }
    }
    return report;
}

TabVersion SearchService::load(const TabVersion& version) {
    for (auto& provider : providers_) {
        if (provider->name() == version.provider) return provider->load(version);
    }
    throw ProviderError("Proveedor desconocido: " + version.provider);
}
} // namespace tabs
