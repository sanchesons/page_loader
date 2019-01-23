#include "executor.h"
#include "resolver.h"
#include "stream.h"
#include "url_parser.h"
#include "http_client.h"

#include <iostream>
#include <string_view>
#include <cstring>

int main(int argc, const char* args[])
{
    if(argc != 2) {

        std::cerr << "Bad input. Correct: file_loader <url>" << std::endl;
        return 1;
    }

    auto [error_url, url] = HttpUrlParser::parse(std::string_view(args[1], std::strlen(args[1])));
    if(error_url) {

        std::cerr << "Bad url" << std::endl;
        return 1;
    }

    if(url.scheme != "http") {

        std::cerr << "Bad url: use http protocol only" << std::endl;
        return 1;
    }

    Loop loop;
    HttpClient client(loop, std::move(url));

    auto out=OutFileStream(loop, "result.txt");
    client.load_stream([&out](std::string_view part_body, const Error& error) {

        if(error) {

            std::cerr << "Error load data" << std::endl;
            return;
        }

        auto data=std::make_shared<std::string>(part_body.begin(), part_body.end());
        out.write(*data, [data](size_t transferd_bytes, const Error& error){

            if(error) {

                std::cerr << "Error write data" << std::endl;
                return;
            }
        });
    });

    loop.run();

    std::cout << "Saved: result.txt" << std::endl;

    return 0;
}