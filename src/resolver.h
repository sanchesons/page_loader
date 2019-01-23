#pragma once

#include "endpoint.h"
#include "error.h"
#include "executor.h"

#include <string_view>
#include <memory>
#include <string>
#include <iostream>
#include <netdb.h>
#include <optional>

using namespace std::string_literals;

namespace
{

class RequestResolve
{
friend int async_resolve_request(RequestResolve& );
friend int chack_resolve(RequestResolve& request);
friend std::tuple<bool, std::vector<Endpoint>> get_result(RequestResolve& request);

private:
    std::unique_ptr<gaicb[]> m_request;
    gaicb* m_ptr;
    std::string_view m_hostname;

public:
    RequestResolve(std::string_view hostname) :
        m_request(std::make_unique<gaicb[]>(1)),
        m_ptr(m_request.get()),
        m_hostname(hostname)
    {
        m_request[0].ar_name = hostname.data();
    }
};

int async_resolve_request(RequestResolve& request)
{
    return getaddrinfo_a(GAI_NOWAIT, &request.m_ptr, 1, nullptr);
}

int chack_resolve(RequestResolve& request)
{
    return gai_error(request.m_request.get());
}

std::tuple<bool, std::vector<Endpoint>> get_result(RequestResolve& request)
{
    std::vector<Endpoint> result;
    std::vector<char> m_addr_buff(NI_MAXHOST);

    for(auto it=request.m_request[0].ar_result; it != nullptr; it=it->ai_next) {

        auto ret=getnameinfo(it->ai_addr, it->ai_addrlen, m_addr_buff.data(), m_addr_buff.size(), nullptr, 0, NI_NUMERICHOST);
        if(ret == 0) {

            result.emplace_back(std::string_view(m_addr_buff.data(), m_addr_buff.size()));
        } else {

            return {true, {}};
        }
    }

    return {false, std::move(result)};
}

}

template<typename T>
void resolve(Loop& loop, std::string_view hostname, T&& handler)
{
    auto request=std::make_shared<RequestResolve>(hostname);
    auto ret=async_resolve_request(*request);

    if (!ret) {

        auto task=[request, handler=std::forward<T>(handler)]() -> bool {

            auto ret=chack_resolve(*request);
            if (ret==EAI_INPROGRESS) {

                return false;
            } else if(ret == 0) {

                if(auto [error, result]=get_result(*request); !error) {

                    handler(result, Error(Error::ok));
                }
                return true;
            } else {

                handler(std::vector<Endpoint>(), Error(Error::err_hostname_resolve, gai_strerror(ret)));
                return true;
            }

            return false;
        };

        loop.post(std::move(task));
    } else {

        handler(std::vector<Endpoint>(), Error(Error::err_hostname_resolve, gai_strerror(ret)));
    }
}