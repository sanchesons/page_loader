#include <ostream>
#include <arpa/inet.h>

class Endpoint
{
private:
    std::string m_addr_str;
    uint32_t m_addr;

public:
    explicit Endpoint(std::string addr) :
        m_addr_str(std::move(addr)),
        m_addr(inet_addr(m_addr_str.data()))
    {}

    explicit Endpoint(std::string_view addr) :
        m_addr_str(addr.data(), addr.size()),
        m_addr(inet_addr(m_addr_str.data()))
    {}

    Endpoint(const Endpoint& other) :
        Endpoint(other.m_addr_str)
    {}

    Endpoint(Endpoint&& other)
    {}

    uint32_t addr() const
    {
        return m_addr;
    }

    friend std::ostream& operator<<(std::ostream& out, const Endpoint& ep)
    {
        return out << ep.m_addr_str;
    }
};

class TcpEndpoint : public Endpoint
{
private:
    uint16_t m_port;

public:
    TcpEndpoint(const Endpoint& endpoint, uint16_t port) :
        Endpoint(endpoint),
        m_port(port)
    {
    }

    TcpEndpoint(Endpoint&& endpoint, uint16_t port) :
        Endpoint(std::forward<Endpoint>(endpoint)),
        m_port(port)
    {
    }

    uint16_t port() const
    {
        return m_port;
    }
};