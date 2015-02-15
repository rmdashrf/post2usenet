#ifndef UTIL_ASIO_HELPERS_HPP
#define UTIL_ASIO_HELPERS_HPP

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace p2u
{
    namespace asio
    {
        const std::string CRLF = "\r\n";
        template <class AsyncReadStream, class CompletionHandler>
        void async_read_line(AsyncReadStream& stream, boost::asio::streambuf& buffer, CompletionHandler handler)
        {
            boost::asio::async_read_until(stream, buffer, CRLF,
                    [handler, &buffer](const boost::system::error_code& ec, size_t bytes_read)
                    {
                        if (!ec)
                        {
                            std::string read_line(boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_begin(buffer.data()) + bytes_read);
                            buffer.consume(bytes_read);
                            handler(ec, read_line);
                        }
                        else
                        {
                            handler(ec, "");
                        }
                    });
        }
    }
}
#endif
