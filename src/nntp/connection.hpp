#ifndef NNTP_CONNECTION_HPP_
#define NNTP_CONNECTION_HPP_

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/stream.hpp>
#include "../util/asio_helpers.hpp"


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
                using post_handler = std::function<void(const std::shared_ptr<article>&, post_result)>;
                using stat_handler = std::function<void(const std::string& msgid, stat_result)>;

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
                tcp::resolver m_resolver;
                state m_state; // because boost MSL would be overkill
                const connection_info& m_conninfo;
                boost::asio::streambuf m_readbuf;
                std::unique_ptr<ssl_context> m_sslctx;
                std::unique_ptr<ssl_stream> m_sslstream;

                boost::asio::deadline_timer m_timer;

                connect_handler m_connecthandler;
                post_handler m_posthandler;
                stat_handler m_stathandler;

                std::shared_ptr<article> m_article;
                std::vector<boost::asio::const_buffer> m_send_parts;
                std::string m_postheader;

                std::string m_msgid;

                void do_connect();

                void do_authenticate();

                void do_post();
                void send_article();
                void do_stat();

                void initSSL();


                template <class CompletionHandler>
                void read_line(CompletionHandler handler)
                {

                    timeout_next_async_operation(5);
                    auto _dispatch = [this,handler](const boost::system::error_code& ec, const std::string& line)
                    {
                        if (!ec)
                        {
                            m_timer.cancel();
                        }

                        handler(ec, line);
                    };

                    if (m_sslstream)
                    {
                        p2u::asio::async_read_line(*m_sslstream, m_readbuf, _dispatch);
                    } else
                    {
                        p2u::asio::async_read_line(m_sock, m_readbuf, _dispatch);
                    }
                }

                void timeout_next_async_operation(int seconds);
                void cancel_sock_operation(const boost::system::error_code& ec);

                template <class ConstBufferSequence, class CompletionHandler>
                void write(const ConstBufferSequence& buffers, CompletionHandler completion_handler)
                {
                    timeout_next_async_operation(5);

                    auto _complete = [this,completion_handler](const boost::system::error_code& ec, size_t bytes_transferred)
                    {
                        m_timer.cancel();
                        completion_handler(ec, bytes_transferred);
                    };

                    if (m_sslstream)
                    {
                        boost::asio::async_write(*m_sslstream, buffers, _complete);
                    }
                    else
                    {
                        boost::asio::async_write(m_sock, buffers, _complete);
                    }
                }

                void send_authinfo_username();
                void send_authinfo_password();
                void send_stat_cmd(const std::string& mid);

                void check_authinfo_result(const std::string& line);
                void post_handler_callback(post_result result);
                void connect_handler_callback(connect_result result);
                void stat_handler_callback(stat_result result);

            public:
                connection(boost::asio::io_service& io_service,
                           const connection_info& conn);

                void set_post_handler(const post_handler& handler);
                void set_connect_handler(const connect_handler& handler);
                void set_stat_handler(const stat_handler& handler);

                void async_connect();
                bool async_post(const std::shared_ptr<article>& message);

                bool async_stat(const std::string& messageid);
                void close();
                boost::asio::io_service& get_io_service();
                ~connection();
        };
    }
}
#endif
