#ifndef NNTP_CONNECTION_HPP_
#define NNTP_CONNECTION_HPP_

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/stream.hpp>


using namespace boost::asio::ip;

namespace p2u
{
    namespace nntp
    {

        struct connection_info;

        // TODO: Is this necessary?
        struct protocol_error : public std::runtime_error
        {
            public:
                protocol_error() : std::runtime_error("Protocol error")
                {

                }
        };

        class article;

        enum class post_result
        {
            POST_SUCCESS,
            POSTING_NOT_PERMITTED,
            POST_FAILURE,
        };

        class connection : private boost::noncopyable
        {
            using ssl_context = boost::asio::ssl::context;
            using ssl_stream = boost::asio::ssl::stream<tcp::socket>;

            enum class state
            {
                DISCONNECTED,
                CONNECTING,
                CONNECTING_AUTHENTICATING,
                CONNECTED_AND_AUTHENTICATED,
            };

            public:
                using connect_handler = std::function<void()>;
                using post_handler = std::function<void(post_result)>;

            private:
                tcp::socket m_sock;
                state m_state;
                const connection_info& m_conninfo;
                boost::asio::streambuf m_readbuf;

                void do_connect(std::function<void()> completion_handler,
                        boost::asio::yield_context yield);

                bool do_authenticate(boost::asio::yield_context yield);

                void do_post(std::shared_ptr<article> message,
                        post_handler handler, boost::asio::yield_context yield);

            public:
                connection(boost::asio::io_service& io_service,
                           const connection_info& conn);

                void async_connect(connect_handler handler);
                void async_post(std::shared_ptr<article> message,
                                post_handler handler);
                ~connection();
        };
    }
}
#endif
