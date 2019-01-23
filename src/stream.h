#pragma once

#include <sys/socket.h>
#include <sys/epoll.h>
#include <iostream>
#include <string.h>
#include <deque>

#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <aio.h>

class LinuxFd
{
private:
    int m_obj;

public:
    explicit LinuxFd(int fd):
        m_obj(fd)
    {}

    LinuxFd(LinuxFd&& fd):
        m_obj(fd.m_obj)
    {
        fd.m_obj = -1;
    }

    LinuxFd(const LinuxFd& fd) = delete;
    LinuxFd& operator=(const LinuxFd& fd) = delete;

    ~LinuxFd()
    {
        if (m_obj != -1) {

            close(m_obj);
        }
    }

    int get()
    {
        return m_obj;
    }
};


class TcpStream
{
private:
    static const size_t EPOLL_SIZE = 10;

private:
    Loop& m_loop;
    LinuxFd m_sock;
    LinuxFd m_epfd;

private:
    LinuxFd create_socket()
    {
        int sock=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if(sock < 0) {

            throw Error(Error::err_init_socket, strerror(errno));
        }

        return LinuxFd(sock);
    }

    LinuxFd create_epoll(size_t size)
    {
        int epfd=epoll_create(size);
        if(epfd < 0) {

            throw Error(Error::err_init_socket, strerror(errno));
        }

        return LinuxFd(epfd);
    }

public:
    explicit TcpStream(Loop& loop):
        m_loop(loop),
        m_sock(create_socket()),
        m_epfd(create_epoll(EPOLL_SIZE))
    {
        epoll_event ev;
        ev.events=EPOLLOUT | EPOLLIN;
        ev.data.fd=m_sock.get();
        if(epoll_ctl(m_epfd.get(), EPOLL_CTL_ADD, m_sock.get(), &ev) < 0) {

            throw Error(Error::err_init_socket, strerror(errno));
        }
    }

    TcpStream(TcpStream&& other) :
        m_loop(other.m_loop),
        m_sock(std::move(other.m_sock)),
        m_epfd(std::move(other.m_epfd))
    {
    }

    ~TcpStream()
    {
    }

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    template<typename T>
    void connect(const TcpEndpoint& ep, T&& handler)
    {
        sockaddr_in addr;
        addr.sin_family=AF_INET;
        addr.sin_port=htons(ep.port());
        addr.sin_addr.s_addr=ep.addr();

        auto ret=::connect(m_sock.get(), (struct sockaddr *)&addr, sizeof(addr));
        if(ret < 0 && errno != EINPROGRESS) {

            handler(Error(Error::err_connect, strerror(errno)));
            return;
        } else if(ret == 0) {

            handler(Error(Error::ok));
            return;
        }

        auto task=[this, handler=std::forward<T>(handler)](){

            epoll_event event;
            int nfds=epoll_wait(m_epfd.get(), &event, 10, 500);
            if(nfds > 0) {

                handler(Error(Error::ok));
                return true;
            } else if(nfds < 0) {

                handler(Error(Error::err_connect, strerror(errno)));
                return true;
            }

            return false;
        };

        m_loop.post(std::move(task));
    }

    template<typename T>
    void write(std::string& data, T&& handler)
    {
        auto ret=::send(m_sock.get(), data.data(), data.size(), MSG_DONTWAIT);
        if(ret == -1) {

            handler(Error(Error::err_write_file, strerror(errno)));
            return ;
        }

        auto task=[this, handler=std::forward<T>(handler)]() {

            epoll_event event;
            int nfds=epoll_wait(m_epfd.get(), &event, 10, 100);
            if(nfds > 0) {

                if(event.events & EPOLLOUT) {

                    handler(Error(Error::ok));
                    return true;
                }
                return false;
            } else if(nfds < 0) {

                handler(Error(Error::err_write_file, strerror(errno)));
                return true;
            }

            return false;
        };

        m_loop.post(std::move(task));
    }

    template<typename T>
    void read_some(std::vector<char>& buffer, T&& handler)
    {
        auto task=[this, buffer_data=buffer.data(), buffer_len=buffer.size(), handler=std::forward<T>(handler)]() {

            epoll_event event;
            auto nfds=epoll_wait(m_epfd.get(), &event, 10, 100);
            if(nfds > 0) {

                if(event.events & EPOLLIN) {

                    auto ret=::recv(m_sock.get(), buffer_data, buffer_len, MSG_DONTWAIT);
                    if(ret == -1 && errno != EAGAIN) {

                        handler(0, Error(Error::err_read_file, strerror(errno)));
                    } else if(ret == 0) {

                        handler(0, Error(Error::err_eof));
                    } else {

                        handler(ret, Error(Error::ok));
                    }
                    return true;
                } else {

                    return false;
                }
                return true;
            } else if(nfds < 0) {

                handler(0, Error(Error::err_read_file, strerror(errno)));
                return true;
            }

            return false;
        };

        m_loop.post(std::move(task));
    }
};


class OutFileStream
{
private:
    Loop& m_loop;
    LinuxFd m_file;
    std::deque<aiocb> m_queue_cb;

private:
    LinuxFd create_file(const char* file_name)
    {
        int fd=open(file_name, O_CREAT | O_APPEND | O_WRONLY | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if(fd==-1) {

            if(errno==EEXIST) {

                if(remove(file_name)==-1) {

                    throw Error(Error::err_init_out_file, strerror(errno));
                }

                int fd=open(file_name, O_CREAT | O_APPEND | O_WRONLY | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if(fd==-1) {

                    throw Error(Error::err_init_out_file, strerror(errno));
                }
                return LinuxFd(fd);
            }
        }

        return LinuxFd(fd);
    }

public:
    explicit OutFileStream(Loop& loop, const char* file_name):
        m_loop(loop),
        m_file(create_file(file_name))
    {
    }

    OutFileStream(OutFileStream&& other) :
        m_loop(other.m_loop),
        m_file(std::move(other.m_file))
    {
    }

    ~OutFileStream()
    {
    }

    template<typename T>
    void write(std::string& data, T&& handler)
    {
        m_queue_cb.emplace_back();
        aiocb& ocb=m_queue_cb.back();
        ocb.aio_nbytes = data.size();
        ocb.aio_fildes = m_file.get();
        ocb.aio_offset = 0;
        ocb.aio_buf = data.data();

        auto ret=::aio_write(&ocb);
        if(ret == -1) {

            handler(0, Error(Error::err_write_file, strerror(errno)));
            return ;
        }

        auto task=[this, ocb_ptr=&ocb, handler=std::forward<T>(handler)]() {

            auto res=aio_error(ocb_ptr);
            if(res==0) {

                int res_bytes=aio_return(ocb_ptr);
                if(res_bytes!=-1){

                    handler(res_bytes, Error(Error::ok));
                    return true;
                } else {

                    handler(0, Error(Error::err_write_file, strerror(errno)));
                    return true;
                }
            } else if(res==EINPROGRESS) {

                return false;
            }
            handler(0, Error(Error::err_write_file));
            return true;
        };

        m_loop.post(std::move(task));
    }
};