#include <string>

class Error
{
public:
    enum Code
    {
        ok,
        err_hostname_resolve,
        err_connect,
        err_read_file,
        err_write_file,
        err_eof,
        err_init_socket,
        err_init_out_file,
        err_parse_header,
        err_large_header,
        err_large_body,
        err_undefined
    };

private:
    Code m_code;
    std::string m_msg;

public:
    Error(Code code, const std::string& msg) :
        m_code(code),
        m_msg(msg)
    {}

    Error(Code code, const char* msg) :
        m_code(code),
        m_msg(msg)
    {}

    Error(Code code) :
        m_code(code),
        m_msg("error code="+std::to_string(code))
    {}

    operator bool() const
    {
        return m_code != ok;
    }

public:
    const std::string& message() const
    {
        return m_msg;
    }
};