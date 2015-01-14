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

        namespace protocol {
            extern const std::string CRLF;
            extern const std::string MESSAGE_TERM;
            extern const std::string POST;
            extern const std::string AUTHINFOUSER;
            extern const std::string AUTHINFOPASS;
            extern const std::string STAT;
        }

        class article;

        enum class post_result
        {
            POST_SUCCESS,
            POSTING_NOT_PERMITTED,
            POST_FAILURE,
            POST_FAILURE_CONNECTION_ERROR
        };

        enum class connect_result
        {
            FATAL_CONNECT_ERROR,
            INVALID_CREDENTIALS,
            CONNECT_SUCCESS
        };

        enum class stat_result
        {
            ARTICLE_EXISTS,
            INVALID_ARTICLE,
            CONNECTION_ERROR
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
                using connect_handler = std::function<void(connect_result)>;
                using post_handler = std::function<void(post_result)>;
                using stat_handler = std::function<void(stat_result)>;

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
                            lock();
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

                boost::asio::deadline_timer m_timer;

                void do_connect(connect_handler completion_handler,
                                boost::asio::yield_context yield);

                bool do_authenticate(boost::asio::yield_context& yield);

                void do_post(const std::shared_ptr<article>& message,
                             post_handler handler,
                             boost::asio::yield_context yield);

                void do_stat(const std::string& messageid,
                             stat_handler handler,
                             boost::asio::yield_context yield);

                void initSSL();

                std::string read_line(boost::asio::yield_context& yield);

                void cancel_sock_operation(const boost::system::error_code& ec);

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

                void send_authinfo_username(boost::asio::yield_context& yield);
                void send_authinfo_password(boost::asio::yield_context& yield);
                void send_stat_cmd(const std::string& mid,
                                   boost::asio::yield_context& yield);

            public:
                connection(boost::asio::io_service& io_service,
                           const connection_info& conn);

                void async_connect(connect_handler handler);
                bool async_post(const std::shared_ptr<article>& message,
                                post_handler handler);

                bool async_stat(const std::string& messageid,
                                stat_handler handler);
                void close();
                boost::asio::io_service& get_io_service();
                ~connection();
        };
    }
}
#endif
