---
order: 60
description: A builder with chained setters for an object with many optional settings
includes: <string> <utility> <vector>
---
//     const Request request = RequestBuilder("https://example.com/api").method("POST").body("{}").build();
// For a plain struct, C++20 designated initializers often do instead: Request{.url = "...", .timeoutSeconds = 5}
// (the {} on members stop g++ -Wextra warning about the ones such an initializer leaves out).
struct Request
{
    std::string url{};
    std::string method = "GET";
    std::vector<std::pair<std::string, std::string>> headers{};
    std::string body{};
    int timeoutSeconds = 30;
};

class RequestBuilder
{
public:
    explicit RequestBuilder(std::string url) { request_.url = std::move(url); }

    RequestBuilder& method(std::string method)
    {
        request_.method = std::move(method);
        return *this;
    }

    RequestBuilder& header(std::string name, std::string value)
    {
        request_.headers.emplace_back(std::move(name), std::move(value));
        return *this;
    }

    RequestBuilder& body(std::string body)
    {
        request_.body = std::move(body);
        return *this;
    }

    RequestBuilder& timeout(int seconds)
    {
        request_.timeoutSeconds = seconds;
        return *this;
    }

    Request build() const { return request_; }

private:
    Request request_;
};
