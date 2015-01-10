#ifndef UTIL_ASIO_HELPERS_HPP
#define UTIL_ASIO_HELPERS_HPP

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace p2u
{
    namespace asio
    {
        template <class AsyncReadStream>
        std::string async_read_line(AsyncReadStream& stream, boost::asio::streambuf& buffer, boost::asio::yield_context& yield)
        {
            size_t bytes_read = boost::asio::async_read_until(stream, buffer, '\n', yield);
            //buffer.commit(bytes_read);
            std::string read_line(boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_end(buffer.data()) + bytes_read);
            //std::istream read_stream(&buffer);
            //std::string read_line;
            //read_line.reserve(bytes_read);

            //std::getline(read_stream, read_line);

            buffer.consume(bytes_read);

            return read_line;
        }
    }
}
#endif
