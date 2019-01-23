#pragma once

#include "url_parser.h"
#include "stream.h"
#include "executor.h"
#include "resolver.h"

#include <deque>

enum class HttpVersion
{
    HTTP_11=11,
    HTTP_10=10
};

using StatusCode=uint8_t;

class Fields
{
private:
    std::unordered_map<std::string_view, std::string_view> m_params;

public:
    void add(std::string_view key, std::string_view value)
    {
        m_params.emplace(key, value);
    }

    std::string_view operator[](std::string_view key)
    {
        return m_params[key];
    }

    auto find(std::string_view key) const
    {
        return m_params.find(key);
    }

    auto begin() const
    {
        return m_params.begin();
    }

    auto end() const
    {
        return m_params.end();
    }
};

struct ResponseHeader : public Fields
{
private:
    mutable std::optional<size_t> m_content_length;

public:
    HttpVersion version;
    StatusCode status_code;
    std::string_view reason_phrase;

    size_t content_length() const
    {
        if(m_content_length){

            return *m_content_length;
        }

        auto it=find("Content-Length"sv);
        if(it==end()) {

            it=find("content-length"sv);
        }

        if(it!=end()) {

            uint32_t number=0;

            if(auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data()+it->second.size(), number); ec==std::errc()) {

                m_content_length=number;
                return *m_content_length;
            }
        }

        m_content_length=0;
        return *m_content_length;
    }
};

class ResponseHeaderParser
{
public:
    enum State
    {
        PROTOCOL_STATUS,
        FILEDS
    };

public:

    static std::tuple<bool, HttpVersion, size_t> parse_version(const std::string_view& data, size_t pos)
    {
        if(auto ppos=data.find('/', pos); ppos!=std::string_view::npos && data.substr(pos, ppos-pos) == "HTTP"sv) {

            pos=ppos+1;
        } else {

            return {true, HttpVersion::HTTP_11, 0};
        }

        if(auto ppos=data.find(' ', pos); ppos!=std::string_view::npos) {

            auto version=data.substr(pos, ppos-pos);
            if(version == "1.1"sv) {

                return {false, HttpVersion::HTTP_11, ppos+1};
            } else if(version == "1.0"sv){

                return {false, HttpVersion::HTTP_10, ppos+1};
            }
        }

        return {true, HttpVersion::HTTP_11, 0};
    }

    static std::tuple<bool, StatusCode, size_t> parse_status_code(const std::string_view& data, size_t pos)
    {
        if(auto ppos=data.find(' ', pos); ppos!=std::string_view::npos) {

            auto status=data.substr(pos, ppos-pos);
            uint32_t number;
            if(auto [ptr, ec] = std::from_chars(status.data(), status.data()+status.size(), number); ec==std::errc()) {

                if(number>=100 && number<600) {

                    return {false, number, ppos+1};
                }
            }
        }
        return {true, 0, 0};
    }

    static std::tuple<bool, ResponseHeader> parse(std::string_view data)
    {
        ResponseHeader header;

        auto pos=size_t(0);
        const auto status_line=data.substr(pos, data.find("\r\n"sv, pos));
        if(auto [error, version, ppos]=parse_version(status_line, pos); !error) {

            pos=ppos;
            header.version=version;
        } else {

            return {true, {}};
        }

        if(auto [error, status_code, ppos]=parse_status_code(status_line, pos); !error) {

            pos=ppos;
            header.status_code=status_code;
        } else {

            return {true, {}};
        }

        header.reason_phrase = status_line.substr(pos);
        pos=status_line.size()+2;
        for(;pos < data.size();){

            if(auto ppos=data.find(':', pos); ppos!=std::string_view::npos) {

                ++ppos;
                if(auto pppos=data.find("\r\n"sv, ppos); pppos!=std::string_view::npos) {

                    header.add(data.substr(pos, ppos-pos-1), data.substr(data.find_first_not_of(' ', ppos), pppos-ppos));
                    pos=pppos+2;
                } else {

                    return {true, {}};
                }
            } else {

                return {true, {}};
            }
        }

        return {false, header};
    }
};


class HttpClient
{
private:
    static constexpr uint32_t MAX_HEADER_SIZE=4096; //bytes
    static constexpr uint32_t MAX_BODY_SIZE=1024*1024*1024; //bytes
    static constexpr uint32_t BUFFER_SIZE=1024; //bytes

    Loop& m_loop;
    TcpStream m_stream;
    std::shared_ptr<std::vector<char>> buffer;
    std::shared_ptr<std::vector<char>> header_buffer;
    HttpUrl m_url;
    std::function<void(std::string_view, const Error&)> m_load_cb;
    std::function<void(const Error&)> m_connect_cb;
    ResponseHeader header;

private:
    void read_http_response_body(size_t bytes_alrady_readed)
    {
        m_stream.read_some(*buffer, [this, bytes_alrady_readed](size_t bytes_readed, const Error& error) {

            if(error) {

                m_load_cb({}, error);
                return;
            }

            m_load_cb(std::string_view(buffer->data(), bytes_readed), Error(Error::ok));
            auto total_readed=bytes_alrady_readed+bytes_readed;
            if(total_readed <= MAX_BODY_SIZE) {

                if(total_readed<header.content_length()) {

                    read_http_response_body(total_readed);
                }
            } else {

                m_load_cb({}, Error(Error::err_large_body) );
            }
        });
    }

    void read_http_response_header()
    {
        m_stream.read_some(*buffer, [this](size_t bytes_readed, const Error& error) {

            if(error) {

                m_load_cb({}, error);
                return;
            }

            if(bytes_readed == 0) {

                return ;
            }

            auto data=std::string_view(buffer->data(), bytes_readed);
            bool is_finish=false;
            auto pos=size_t(0);
            for(; pos<data.size() && !is_finish; pos+=2) {

                auto next_pos=data.find("\r\n"sv, pos);
                if(next_pos==std::string_view::npos) {

                    m_load_cb({}, Error(Error::err_parse_header));
                    return;
                }

                is_finish=(pos==next_pos);
                if(!is_finish) {

                    header_buffer->insert(header_buffer->end(), data.data()+pos, data.data()+next_pos+2);
                }

                pos=next_pos;
            }

            if(is_finish) {

                bool error;
                std::tie(error, header)=ResponseHeaderParser::parse(std::string_view(header_buffer->data(),header_buffer->size()));
                if(!error) {

                    if(header.content_length()>0) {

                        auto bytes_alrady_readed=data.size()-pos;
                        if(pos!=data.size()) {

                            m_load_cb(data.substr(pos, bytes_alrady_readed), Error(Error::ok));
                        }
                        read_http_response_body(bytes_alrady_readed);
                    }
                } else {

                    m_load_cb({}, Error(Error::err_parse_header));
                }
            } else {

                if(header_buffer->size() <= MAX_HEADER_SIZE) {

                    read_http_response_header();
                } else {

                    m_load_cb({}, Error(Error::err_large_header));
                }
            }
        });
    }

    void send_http_request()
    {
        auto http_request = std::make_shared<std::string>();
        http_request->reserve(MAX_HEADER_SIZE);
        http_request->append("GET "sv);
        http_request->append(m_url.target);
        http_request->append(" HTTP/1.1\r\n"sv);
        http_request->append("Host: "sv);
        http_request->append(m_url.host);
        http_request->append("\r\n"sv);
        http_request->append("Accept: text/html\r\n"sv);
        http_request->append("User-Agent: Test\r\n"sv);
        http_request->append("\n"sv);

        m_stream.write(*http_request, [this, http_request](const Error& error) {

            if(error) {

                m_load_cb({}, error);
                return;
            }

            read_http_response_header();
        });
    }


public:
    HttpClient(Loop& loop, HttpUrl&& url):
        m_loop(loop),
        m_stream(m_loop),
        buffer(std::make_shared<std::vector<char>>(BUFFER_SIZE)),
        header_buffer(std::make_shared<std::vector<char>>()),
        m_url(std::forward<HttpUrl>(url))
    {
        header_buffer->reserve(MAX_HEADER_SIZE);
    }

    template<typename T>
    void connect(T&& handler)
    {
        if(m_connect_cb) {

            return;
        }

        m_connect_cb=std::forward<T>(handler);

        resolve(m_loop, m_url.host, [this](const auto& result, const Error& error) {

            if(error) {

                m_connect_cb(error);
                return;
            }

            m_stream.connect(TcpEndpoint(result.front(), m_url.port), [this](const Error& error) {

                m_connect_cb(error);
            });
        });
    }

    template<typename T>
    void load_stream(T&& handler)
    {
        if(m_load_cb) {

            return;
        }

        m_load_cb=std::forward<T>(handler);
        connect([this](const auto& error){

            if(error) {

                m_load_cb({}, error);
                return;
            }

            send_http_request();
        });
    }
};