#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <thread>
#include <cstddef>
#include <boost/algorithm/string.hpp>
#include <string>
#include <unistd.h>
#include <fstream>

using namespace boost::asio::ip;

using ssl_socket = boost::asio::ssl::stream<tcp::socket>;
using ssl_ctx = boost::asio::ssl::context;

const std::string SERVER_WELCOME("200 Some Generic Welcome Message (Posting Allowed)\r\n");
const std::string SERVER_AUTH("381 PASS required\r\n");
const std::string SERVER_AUTH_SUCCESS("281 Posting allowed\r\n");
const std::string SERVER_SEND_ARTICLE("381 Send article to be posted\r\n");
const std::string SERVER_ARTICLE_POSTED("200 Article posted\r\n");

std::string read_line(ssl_socket& socket, boost::asio::streambuf& readbuf)
{
    size_t bytes_read = boost::asio::read_until(socket, readbuf, "\r\n");
    auto buffer = readbuf.data();
    std::string line(boost::asio::buffers_begin(buffer), boost::asio::buffers_begin(buffer)+bytes_read);
    readbuf.consume(bytes_read);
    return line;
}

void usenet_session(std::unique_ptr<ssl_socket> sockptr)
{
    ssl_socket& socket = *sockptr;
    boost::asio::streambuf readbuf;

    const auto& id = sockptr->lowest_layer().native_handle();
    //std::cout << "Starting session with " << id << std::endl;
    try {
     //   std::cout << "Trying to handshake with: " << id << std::endl;
        socket.handshake(ssl_socket::handshake_type::server);
      //  std::cout << "Finished handshaking with " << id << std::endl;
        boost::asio::write(socket, boost::asio::buffer(SERVER_WELCOME));
       // std::cout << "Wrote server welcome to " << id << std::endl;
        std::string line = read_line(socket, readbuf);
        if (!boost::algorithm::starts_with(line, "AUTHINFO USER"))
        {
            std::cout << "violated protocol. expected authinfo " << std::endl;
            socket.shutdown();
            return;
        }

    boost::asio::write(socket, boost::asio::buffer(SERVER_AUTH));
    line = read_line(socket, readbuf);
    if (!boost::algorithm::starts_with(line, "AUTHINFO PASS"))
    {
        std::cout << "violated protocol. expected authinfo pass " << std::endl;
        socket.shutdown();
        return;
    }
    boost::asio::write(socket, boost::asio::buffer(SERVER_AUTH_SUCCESS));
    while (true)
    {
        line = read_line(socket, readbuf);
        if (boost::algorithm::starts_with(line, "POST"))
        {
            boost::asio::write(socket, boost::asio::buffer(SERVER_SEND_ARTICLE));
            std::string tmpfilename{"/tmp/postXXXXXX"};
            int fd = mkstemp((char*)tmpfilename.c_str());
            std::ofstream outfile{tmpfilename};

            do {
                line = read_line(socket, readbuf);
                outfile.write(line.c_str(), line.size());
        //        std::cout << "Got line: " << line << std::endl;
            } while (line != ".\r\n");

            outfile.close();
            close(fd);
         //   std::cout << "Writing article posted " << std::endl;
            boost::asio::write(socket, boost::asio::buffer(SERVER_ARTICLE_POSTED));
        } else if (boost::algorithm::starts_with(line, "QUIT"))
        {
            std::cout << "Client " << id << " gracefully quitting " << std::endl;
            socket.shutdown();
        }
    }
    } catch (std::exception& e)
    {
        std::cout << "client: " << sockptr->native_handle() << " some error " << e.what() << " occurred. terminating client" << std::endl;
    }
}

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " port " << std::endl;
        return 1;
    }

    std::uint16_t port = static_cast<std::uint16_t>(std::stoul(argv[1]));
    std::cout << "Listening on port " << port << std::endl;
    boost::asio::io_service io_service;

    ssl_ctx context(ssl_ctx::method::sslv23);
    context.use_certificate_chain_file("server.crt");
    context.use_private_key_file("server.key", ssl_ctx::file_format::pem);
    tcp::acceptor acceptor{io_service, tcp::endpoint(tcp::v4(), port)};

    while (true)
    {
        std::unique_ptr<ssl_socket> socket(new ssl_socket(io_service, context));
        acceptor.accept(socket->lowest_layer());
        std::thread(usenet_session, std::move(socket)).detach();
    }
}
