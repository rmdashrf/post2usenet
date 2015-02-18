/**
 * Welcome to Callback Hell.
 *
 * I tried using Boost Coroutine to make this look prettier, but I kept on
 * getting weird uninitailized memory issues. It was also notoriously difficult
 * to debug since GDB and Valgrind are both coroutine unaware.
 *
 * The state of coroutines in C++, frankly, is not usable. We're just going
 * to have to endure an endless series of callbacks.
 *
 */

#include <iostream>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string.hpp>
#include <array>
#include <cassert>

#include "message.hpp"
#include "connection.hpp"
#include "connection_info.hpp"
#include "../util/asio_helpers.hpp"

const std::string p2u::nntp::protocol::CRLF{"\r\n"};
const std::string p2u::nntp::protocol::MESSAGE_TERM{"\r\n.\r\n"};
const std::string p2u::nntp::protocol::POST{"POST\r\n"};
const std::string p2u::nntp::protocol::AUTHINFOUSER{"AUTHINFO USER "};
const std::string p2u::nntp::protocol::AUTHINFOPASS{"AUTHINFO PASS "};
const std::string p2u::nntp::protocol::STAT{"STAT "};
const std::string p2u::nntp::protocol::QUIT{"QUIT\r\n"};

p2u::nntp::connection::connection(boost::asio::io_service& io_service,
                                  const connection_info& conn)
    : m_sock {io_service}, m_resolver{io_service}, m_state {state::DISCONNECTED}, m_conninfo(conn),
      m_timer{io_service}
{
    if (conn.tls)
    {
        initSSL();
    }
}

p2u::nntp::connection::~connection()
{
}


void p2u::nntp::connection::initSSL()
{
    m_sslctx = std::unique_ptr<ssl_context>(
            new ssl_context(boost::asio::ssl::context::sslv23));

    // TODO: Actually verify peer cert
    // Because we want the NSA to MITM us
    m_sslctx->set_verify_mode(boost::asio::ssl::verify_none);

    m_sslstream = std::unique_ptr<ssl_stream>(
            new ssl_stream(m_sock, *m_sslctx));
}

void p2u::nntp::connection::timeout_next_async_operation(int seconds)
{
    // Start the timer.
    m_timer.expires_from_now(boost::posix_time::seconds(seconds));
    m_timer.async_wait([this](const boost::system::error_code& ec)
            {
                if (!ec)
                {
                    m_sock.cancel();
                }
            });
}

void p2u::nntp::connection::send_authinfo_username()
{
    std::array<boost::asio::const_buffer, 3> parts {
        boost::asio::buffer(protocol::AUTHINFOUSER),
        boost::asio::buffer(m_conninfo.username),
        boost::asio::buffer(protocol::CRLF)
    };

    write(parts, [this](const boost::system::error_code& ec, size_t bytes_transferred)
            {
                if (!ec)
                {
                    read_line([this](const boost::system::error_code& ec, const std::string& line)
                        {
                            if (!ec)
                            {

                                if (boost::starts_with(line, "381"))
                                {
                                    send_authinfo_password();
                                }
                                else
                                {
                                    check_authinfo_result(line);
                                }
                            }
                            else
                            {
                                connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                            }
                        });
                }
                else
                {
                    get_io_service().post(std::bind(m_connecthandler,
                            connect_result::FATAL_CONNECT_ERROR));
                }
            });
}

void p2u::nntp::connection::check_authinfo_result(const std::string& line)
{
    if (line[0] == '2')
    {
        m_state = state::CONNECTED_AND_AUTHENTICATED;
        connect_handler_callback(connect_result::CONNECT_SUCCESS);
    }
    else
    {
        connect_handler_callback(connect_result::INVALID_CREDENTIALS);
    }
}

void p2u::nntp::connection::send_authinfo_password()
{
    std::array<boost::asio::const_buffer, 3> parts {
        boost::asio::buffer(protocol::AUTHINFOPASS),
        boost::asio::buffer(m_conninfo.password),
        boost::asio::buffer(protocol::CRLF)
    };

    write(parts, [this](const boost::system::error_code& ec, size_t bytes_transferred)
            {
                if (!ec)
                {
                    read_line([this](const boost::system::error_code& ec, const std::string& line)
                        {
                            if (!ec)
                            {
                                check_authinfo_result(line);
                            }
                            else
                            {
                                connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                            }
                        });
                }
                else
                {
                    connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                }
            });
}

void p2u::nntp::connection::do_authenticate()
{
    read_line([this](const boost::system::error_code& ec, const std::string& line)
            {
                if (!ec)
                {
                    if (line[0] != '2')
                    {
                        connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                    }
                    else
                    {
                        send_authinfo_username();
                    }
                }
                else
                {
                    connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                }
            });
}

void p2u::nntp::connection::connect_handler_callback(connect_result result)
{
    get_io_service().post(std::bind(m_connecthandler, result));
}

void p2u::nntp::connection::do_connect()
{
    boost::system::error_code ec;

    m_state = state::CONNECTING;

    tcp::resolver::query query(m_conninfo.serveraddr, "");


    m_resolver.async_resolve(query,
            [this](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator resolvit)
            {
                if (!ec)
                {
                    tcp::endpoint endpoint(resolvit->endpoint().address(), m_conninfo.port);
                    timeout_next_async_operation(5);
                    m_sock.async_connect(endpoint, [this](const boost::system::error_code& ec)
                        {
                            if (!ec)
                            {
                                // Connect successful, try authenticating
                                m_timer.cancel();

                                if (m_sslstream)
                                {
                                    // Need to handshake first
                                    timeout_next_async_operation(5);
                                    m_sslstream->async_handshake(m_sslstream->client,
                                        [this](const boost::system::error_code& ec)
                                        {
                                            if (!ec)
                                            {
                                                m_timer.cancel();
                                                do_authenticate();
                                            }
                                            else
                                            {
                                                connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                                            }
                                        });
                                }
                                else
                                {
                                    do_authenticate();
                                }
                            }
                            else
                            {
                                connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                            }
                        });
                }
                else
                {
                    connect_handler_callback(connect_result::FATAL_CONNECT_ERROR);
                }
            });
}

void p2u::nntp::connection::async_connect()
{
    if (m_state == state::DISCONNECTED)
    {
        get_io_service().post([this](){do_connect();});
    }
}


void p2u::nntp::connection::post_handler_callback(post_result result)
{
    m_state = state::CONNECTED_AND_AUTHENTICATED;
    auto article_copy = m_article;
    get_io_service().post([this, article_copy, result]()
            {
                if (m_posthandler)
                {
                    m_posthandler(article_copy, result);
                }
            });

    m_article.reset();
}

void p2u::nntp::connection::do_post()
{
    m_state = state::BUSY;

    write(boost::asio::buffer(protocol::POST),
            [this](const boost::system::error_code& ec, size_t)
            {
                if (!ec)
                {
                    read_line([this](const boost::system::error_code& ec, const std::string& line)
                    {
                        if (!ec)
                        {
                            if (line[0] == '4')
                            {
                                post_handler_callback(post_result::POSTING_NOT_PERMITTED);
                            }
                            else
                            {
                                send_article();
                            }
                        }
                        else
                        {
                            post_handler_callback(post_result::POST_FAILURE_CONNECTION_ERROR);
                        }
                    });
                }
                else
                {
                    post_handler_callback(post_result::POST_FAILURE_CONNECTION_ERROR);
                }
            });

}

void p2u::nntp::connection::send_article()
{
    std::ostringstream header;
    m_article->get_header().write_to(header);
    m_postheader.clear();
    m_send_parts.clear();
    m_postheader = header.str();

    m_send_parts.push_back(boost::asio::buffer(m_postheader.c_str(), m_postheader.length()));
    m_send_parts.push_back(boost::asio::buffer(protocol::CRLF));
    m_article->write_payload_asio_buffers(std::back_inserter(m_send_parts));
    m_send_parts.push_back(boost::asio::buffer(protocol::MESSAGE_TERM));

    write(m_send_parts, [this](const boost::system::error_code& ec, size_t)
            {
                if (!ec)
                {
                    read_line([this](const boost::system::error_code& ec, const std::string& line)
                        {
                            if (!ec)
                            {
                                if (line[0] == '2')
                                {
                                    post_handler_callback(post_result::POST_SUCCESS);
                                }
                                else
                                {
                                    std::cout << "[WARN] Post failure. Server responded: " << line << std::endl;
                                    post_handler_callback(post_result::POST_FAILURE);
                                }
                            }
                            else
                            {
                                post_handler_callback(post_result::POST_FAILURE_CONNECTION_ERROR);
                            }
                        });
                }
                else
                {
                    post_handler_callback(post_result::POST_FAILURE_CONNECTION_ERROR);
                }
            });
}

void p2u::nntp::connection::set_post_handler(const post_handler& handler)
{
    m_posthandler = handler;
}

void p2u::nntp::connection::set_connect_handler(const connect_handler& handler)
{
    m_connecthandler = handler;
}

void p2u::nntp::connection::set_stat_handler(const stat_handler& handler)
{
    m_stathandler = handler;
}

bool p2u::nntp::connection::async_post(const std::shared_ptr<article>& message)
{
    if (m_state != state::CONNECTED_AND_AUTHENTICATED)
    {
        std::cout << "async_post called without being connected and authenticated!" << std::endl;
        return false;
    }

    m_article = message;

    get_io_service().post([this](){ do_post(); });
    return true;
}


void p2u::nntp::connection::stat_handler_callback(stat_result result)
{
    m_state = state::CONNECTED_AND_AUTHENTICATED;
    auto msgid_copy = m_msgid;
    get_io_service().post([this, msgid_copy, result]()
            {
                if (m_stathandler)
                {
                    m_stathandler(msgid_copy, result);
                }
                else
                {
                    std::cout << "[WARN] Stat handler not set. Discarding result for " << m_msgid << std::endl;
                }
            });
    m_msgid.clear();
}

void p2u::nntp::connection::do_stat()
{
    m_state = state::BUSY;

    std::array<boost::asio::const_buffer, 3> parts = {
        boost::asio::buffer(protocol::STAT),
        boost::asio::buffer(m_msgid),
        boost::asio::buffer(protocol::CRLF)
    };

    write(parts, [this](const boost::system::error_code& ec, size_t)
    {
        if (!ec)
        {
            read_line([this](const boost::system::error_code& ec, const std::string& line)
                {
                    if (!ec)
                    {

                        if (line[0] == '2')
                        {
                            stat_handler_callback(stat_result::ARTICLE_EXISTS);
                        }
                        else
                        {
                            // 430 no article with that message-id
                            stat_handler_callback(stat_result::INVALID_ARTICLE);
                        }
                    }
                    else
                    {
                        stat_handler_callback(stat_result::CONNECTION_ERROR);
                    }
                });
        }
        else
        {
            stat_handler_callback(stat_result::CONNECTION_ERROR);
        }
    });
}

bool p2u::nntp::connection::async_stat(const std::string& mid)
{
    if (m_state != state::CONNECTED_AND_AUTHENTICATED)
    {
        assert(false);
        return false;
    }

    m_msgid = mid;

    get_io_service().post([this, mid](){ do_stat(); });
    return true;
}


void p2u::nntp::connection::close()
{
    if (m_state == state::DISCONNECTED)
        return;

    try
    {
        m_sock.shutdown(tcp::socket::shutdown_both);
        m_sock.close();
    }
    catch (std::exception& e)
    {
        // Ignore any exceptions that may occur :(
    }

    // Consume any leftover data in the readbuf
    size_t data_left = m_readbuf.size();
    if (data_left > 0)
    {
        m_readbuf.consume(data_left);
    }

    m_state = state::DISCONNECTED;
}

boost::asio::io_service& p2u::nntp::connection::get_io_service()
{
    return m_sock.get_io_service();
}


void p2u::nntp::connection::cancel_sock_operation(const boost::system::error_code& ec)
{
    if (!ec)
    {
        // Only cancel if the timer actually fired, and was not canceled.
        m_sock.cancel();
    }
}


void p2u::nntp::connection::async_graceful_disconnect()
{
    if (m_state == state::CONNECTED_AND_AUTHENTICATED)
    {
        m_state = state::BUSY;
        write(boost::asio::buffer(p2u::nntp::protocol::QUIT), [this](const boost::system::error_code& ec,size_t)
                {
                    close();
                });
    }
    else
    {
        assert(false);
    }
}
