#pragma once

#include "model.hpp"
#include "chords.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace tabs {

class ProviderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual std::string get(const std::string& url) = 0;
};

class NativeHttpClient final : public HttpClient {
public:
    std::string get(const std::string& url) override;
};

class Provider {
public:
    virtual ~Provider() = default;
    virtual std::string name() const = 0;
    virtual std::vector<TabVersion> search(const std::string& query) = 0;
    virtual TabVersion load(const TabVersion& version) = 0;
};

std::vector<std::unique_ptr<Provider>> makeProviders(HttpClient& http);

struct SearchReport {
    std::vector<TabVersion> versions;
    std::vector<std::string> errors;
};

class SearchService {
public:
    explicit SearchService(HttpClient& http);
    SearchReport search(const std::string& query);
    TabVersion load(const TabVersion& version);

private:
    std::vector<std::unique_ptr<Provider>> providers_;
};

} // namespace tabs
