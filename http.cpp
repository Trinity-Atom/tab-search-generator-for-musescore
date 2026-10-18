#include "providers.hpp"

#include <array>
#include <regex>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace tabs {
namespace {
constexpr size_t maxBody = 4 * 1024 * 1024;

struct Url {
    std::string host;
    std::string path;
};

Url parseUrl(const std::string& url) {
    static const std::regex pattern(R"(^https://([a-zA-Z0-9.-]+)(/[^#\r\n]*)?$)");
    std::smatch match;
    if (!std::regex_match(url, match, pattern))
        throw ProviderError("URL HTTPS invalida: " + url);
    const std::array<std::string, 7> hosts = {
        "acordes.lacuerda.net", "lacuerda.net", "www.lacuerda.net",
        "www.cifraclub.com", "solr.sscdn.co",
        "www.ultimate-guitar.com", "tabs.ultimate-guitar.com"
    };
    const auto host = match[1].str();
    for (const auto& allowed : hosts) {
        if (host == allowed) return {host, match[2].matched ? match[2].str() : "/"};
    }
    throw ProviderError("Dominio no permitido: " + host);
}

struct Response {
    int status{};
    std::string body;
    std::string location;
};

#ifdef _WIN32
class Handle {
public:
    explicit Handle(HINTERNET value) : value_(value) {
        if (!value_) throw ProviderError("WinHTTP: error " + std::to_string(GetLastError()));
    }
    ~Handle() { WinHttpCloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return value_; }
private:
    HINTERNET value_;
};

std::wstring wide(const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw ProviderError("URL no valida en UTF-8");
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}

void check(BOOL ok) {
    if (!ok) throw ProviderError("WinHTTP: error " + std::to_string(GetLastError()));
}

Response request(const std::string& url) {
    const auto parsed = parseUrl(url);
    Handle session(WinHttpOpen(L"TabSearchForMuseScore/1.0",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    check(WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000));
    Handle connection(WinHttpConnect(session, wide(parsed.host).c_str(),
                                     INTERNET_DEFAULT_HTTPS_PORT, 0));
    Handle req(WinHttpOpenRequest(connection, L"GET", wide(parsed.path).c_str(),
                                  nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    check(WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));
    check(WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
    check(WinHttpReceiveResponse(req, nullptr));
    DWORD status = 0, size = sizeof(status);
    check(WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                              WINHTTP_NO_HEADER_INDEX));
    Response result;
    result.status = static_cast<int>(status);
    if (status >= 300 && status < 400) {
        size = 0;
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                 nullptr, &size, WINHTTP_NO_HEADER_INDEX) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            std::wstring location(size / sizeof(wchar_t), L'\0');
            check(WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     location.data(), &size, WINHTTP_NO_HEADER_INDEX));
            location.resize(wcslen(location.c_str()));
            for (wchar_t c : location) {
                if (c > 127) throw ProviderError("Redireccion no codificada");
                result.location += static_cast<char>(c);
            }
        }
        return result;
    }
    std::array<char, 16384> buffer{};
    DWORD read = 0;
    do {
        check(WinHttpReadData(req, buffer.data(), static_cast<DWORD>(buffer.size()), &read));
        if (result.body.size() + read > maxBody)
            throw ProviderError("Respuesta mayor de 4 MiB");
        result.body.append(buffer.data(), read);
    } while (read);
    return result;
}
#else
size_t receive(char* data, size_t size, size_t count, void* context) {
    auto& body = *static_cast<std::string*>(context);
    const size_t bytes = size * count;
    if (body.size() + bytes > maxBody) return 0;
    body.append(data, bytes);
    return bytes;
}

Response request(const std::string& url) {
    parseUrl(url);
    static const auto init = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init != CURLE_OK) throw ProviderError("No se pudo inicializar libcurl");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw ProviderError("No se pudo crear la peticion HTTP");
    Response result;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "TabSearchForMuseScore/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &result.body);
    const auto code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) throw ProviderError(std::string("HTTP: ") + curl_easy_strerror(code));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    result.status = static_cast<int>(status);
    char* redirect = nullptr;
    curl_easy_getinfo(curl.get(), CURLINFO_REDIRECT_URL, &redirect);
    if (redirect) result.location = redirect;
    return result;
}
#endif
} // namespace

std::string NativeHttpClient::get(const std::string& initialUrl) {
    std::string url = initialUrl;
    for (int redirects = 0; redirects <= 5; ++redirects) {
        const auto response = request(url);
        if (response.status >= 300 && response.status < 400 && !response.location.empty()) {
            if (redirects == 5) throw ProviderError("Demasiadas redirecciones");
            if (response.location.rfind("//", 0) == 0) url = "https:" + response.location;
            else if (response.location.front() == '/')
                url = "https://" + parseUrl(url).host + response.location;
            else url = response.location;
            continue;
        }
        if (response.status != 200)
            throw ProviderError("HTTP " + std::to_string(response.status) + " en " + url);
        if (response.body.empty()) throw ProviderError("Respuesta HTTP vacia en " + url);
        return response.body;
    }
    throw ProviderError("No se pudo completar la peticion HTTP");
}
} // namespace tabs
