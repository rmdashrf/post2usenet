#ifndef UTIL_ASIO_HELPERS_HPP
#define UTIL_ASIO_HELPERS_HPP

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace p2u
{
    namespace asio
    {
        template <class AsyncReadStream, class Allocator, class Handler>
        std::string async_read_line(AsyncReadStream& stream, boost::asio::basic_streambuf<Allocator>& buffer, boost::asio::basic_yield_context<Handler> yield)
        {
            size_t bytes_read = boost::asio::async_read_until(stream, buffer, "\n", yield);

            std::istream read_stream(&buffer);
            std::string read_line;
            read_line.reserve(bytes_read);

            std::getline(read_stream, read_line);

            buffer.consume(bytes_read);

            return read_line;
        }
    }
}
#endif
