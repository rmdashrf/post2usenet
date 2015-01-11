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

        extern const std::string CRLF;
        extern const std::string MESSAGE_TERM;
        extern const std::string POST;

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
            using ssl_stream = boost::asio::ssl::stream<tcp::socket&>;

            enum class state
            {
                DISCONNECTED,
                CONNECTING,
                CONNECTING_AUTHENTICATING,
                CONNECTED_AND_AUTHENTICATED,
                BUSY
            };

            public:
                using connect_handler = std::function<void()>;
                using post_handler = std::function<void(post_result)>;

            private:
                /**
                 * Not really a lock, but it changes our state between
                 * CONNECTED_AND_AUTHENTICATED to BUSY in an RAII compatible
                 * manner */
                class busy_state_lock : private boost::noncopyable
                {
                    state& _parent_state;

                    public:
                        busy_state_lock(state& _state)
                            : _parent_state{_state}
                        {

                        }

                        void lock()
                        {
                            _parent_state = state::BUSY;
                        }
                        void unlock()
                        {
                            _parent_state = state::CONNECTED_AND_AUTHENTICATED;
                        }

                        ~busy_state_lock()
                        {
                            unlock();
                        }
                };

                tcp::socket m_sock;
                state m_state; // because boost MSL would be overkill
                const connection_info& m_conninfo;
                boost::asio::streambuf m_readbuf;
                std::unique_ptr<ssl_context> m_sslctx;
                std::unique_ptr<ssl_stream> m_sslstream;
                boost::asio::strand m_strand;

                void do_connect(connect_handler completion_handler,
                                boost::asio::yield_context yield);

                bool do_authenticate(boost::asio::yield_context& yield);

                void do_post(const std::shared_ptr<article>& message,
                             post_handler handler,
                             boost::asio::yield_context yield);

                void initSSL();

                std::string read_line(boost::asio::yield_context& yield);

                template <class ConstBufferSequence>
                size_t write(const ConstBufferSequence& buffers,
                             boost::asio::yield_context yield)
                {
                    if (m_sslstream)
                    {
                        return boost::asio::async_write(*m_sslstream, buffers,
                                                        yield);
                    }
                    else
                    {
                        return boost::asio::async_write(m_sock, buffers, yield);
                    }
                }

            public:
                connection(boost::asio::io_service& io_service,
                           const connection_info& conn);

                void async_connect(connect_handler handler);
                bool async_post(const std::shared_ptr<article>& message,
                                post_handler handler);
                void close();
                boost::asio::io_service& get_io_service();
                ~connection();
        };
    }
}
#endif
