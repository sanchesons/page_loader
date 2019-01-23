#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <charconv>
#include <system_error>

using namespace std::literals;

struct HttpUrl
{
    std::string scheme;
    uint16_t port;
    std::string host;
    std::string target;

    HttpUrl():
        port(0)
    {}

    HttpUrl(HttpUrl&& other):
        scheme(std::move(other.scheme)),
        port(other.port),
        host(std::move(other.host)),
        target(std::move(other.target))
    {}
};

class HttpUrlParser
{
private:
    static std::tuple<bool, size_t, std::string_view> parse_scheme(std::string_view text, size_t pos)
    {
        if(auto ppos=text.find("://"sv, pos); ppos!=std::string_view::npos) {

            auto scheme=text.substr(pos, ppos);
            if(scheme=="http"sv) {

                return {false, ppos+3, scheme};
            }
        }
        return {true, 0, {}};
    }

    static std::tuple<bool, size_t, std::string_view, uint16_t> parse_authority(std::string_view text, size_t pos)
    {
        auto end=text.find('/', pos);
        auto authority=text.substr(pos, end-pos);
        auto port=uint16_t(80);

        if(auto ppos=authority.find(':'); ppos!=std::string_view::npos) {

            auto [p, ec]=std::from_chars(authority.data()+ppos+1, authority.data()+authority.size(), port);
            if(ec==std::errc()) {

                return {false, end, authority.substr(0, ppos), port};
            }
            return {true, 0, {}, 0};
        }
        return {false, end, authority, port};
    }

public:
    static std::tuple<bool, HttpUrl> parse(std::string_view text)
    {
        HttpUrl url;
        auto pos=size_t(0);
        if(auto [error, ppos, scheme]=parse_scheme(text, pos); !error) {

            url.scheme=std::string(scheme.begin(), scheme.end());
            pos=ppos;
        } else {

            return {true, HttpUrl()};
        }

        if(auto [error, ppos, host, port]=parse_authority(text, pos); !error) {

            url.host=std::string(host.begin(), host.end());
            url.port=port;
            pos=ppos;
        } else {

            return {true, HttpUrl()};
        }

        if(pos!=std::string_view::npos) {

            url.target=std::string(text.begin()+pos, text.end());
        } else {

            url.target=std::string("/");
        }

        return {false, std::move(url)};
    }
};
