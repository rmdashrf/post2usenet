#include <iostream>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string.hpp>
#include <array>

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

p2u::nntp::connection::connection(boost::asio::io_service& io_service,
                                  const connection_info& conn)
    : m_sock {io_service}, m_state {state::DISCONNECTED}, m_conninfo(conn),
      m_strand{io_service}, m_timer{io_service}
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

    // Because we want the NSA to MITM us
    m_sslctx->set_verify_mode(boost::asio::ssl::verify_none);

    m_sslstream = std::unique_ptr<ssl_stream>(
            new ssl_stream(m_sock, *m_sslctx));
}

std::string p2u::nntp::connection::read_line(boost::asio::yield_context& yield)
{
    std::string ret;

    // Start the timer.
    m_timer.expires_from_now(boost::posix_time::seconds(5));
    m_timer.async_wait(std::bind(
                &p2u::nntp::connection::cancel_sock_operation, this,
                std::placeholders::_1));

    if (m_sslstream)
    {
        ret = p2u::asio::async_read_line(*m_sslstream, m_readbuf, yield);
    } else
    {
        ret = p2u::asio::async_read_line(m_sock, m_readbuf, yield);
    }

    // If we reach here, this means that the operation was successful. Cancel
    // the timer.
    m_timer.cancel();

    return ret;
}

void p2u::nntp::connection::send_authinfo_username(boost::asio::yield_context& yield)
{
    std::array<boost::asio::const_buffer, 3> parts {
        boost::asio::buffer(protocol::AUTHINFOUSER),
        boost::asio::buffer(m_conninfo.username),
        boost::asio::buffer(protocol::CRLF)
    };

    write(parts, yield);
}

void p2u::nntp::connection::send_authinfo_password(boost::asio::yield_context& yield)
{
    std::array<boost::asio::const_buffer, 3> parts {
        boost::asio::buffer(protocol::AUTHINFOPASS),
        boost::asio::buffer(m_conninfo.password),
        boost::asio::buffer(protocol::CRLF)
    };

    write(parts, yield);
}

bool p2u::nntp::connection::do_authenticate(boost::asio::yield_context& yield)
{
    std::string line = read_line(yield);
    if (line[0] != '2')
    {
        return false;
    }

    send_authinfo_username(yield);
    line = read_line(yield);

    if (boost::starts_with(line, "381"))
    {
        send_authinfo_password(yield);
        line = read_line(yield);
    }

    if (line[0] == '2')
    {
        return true;
    } else
    {
        return false;
    }
}

void p2u::nntp::connection::do_connect(connect_handler completion_handler,
                                       boost::asio::yield_context yield)
{
    boost::system::error_code ec;
    auto& io_service = m_sock.get_io_service();
    try
    {
        m_state = state::CONNECTING;

        tcp::resolver resolver{io_service};
        tcp::resolver::query query(m_conninfo.serveraddr, "");

        for (auto resolvit = resolver.async_resolve(query, yield);
                resolvit != tcp::resolver::iterator(); ++resolvit)
        {

            tcp::endpoint endpoint(resolvit->endpoint().address(),
                                   m_conninfo.port);
            std::cout << "Connecting to " << endpoint << std::endl;

            try {
                m_sock.async_connect(endpoint, yield);
                m_state = state::CONNECTING_AUTHENTICATING;
                break;
            } catch (boost::system::system_error& e)
            {
                std::cout << "Could not connect to " << endpoint << std::endl;
                std::cout << "Error: " << e.what() << std::endl;
            }
        }

        if (m_state != state::CONNECTING_AUTHENTICATING)
        {
            throw std::runtime_error("Could not connect to any endpoints");
        }

        if (m_sslstream)
        {
            m_sslstream->async_handshake(m_sslstream->client, yield);
        }

        if (do_authenticate(yield))
        {
            std::cout << "Authentication successful " << std::endl;
            m_state = state::CONNECTED_AND_AUTHENTICATED;
            m_strand.post(std::bind(completion_handler, connect_result::CONNECT_SUCCESS));
        } else {
            std::cout << "Authentication failed " << std::endl;
            m_state = state::DISCONNECTED;
            m_strand.post(std::bind(completion_handler, connect_result::INVALID_CREDENTIALS));
        }


    } catch (std::exception& e)
    {
        std::cout << "Caught exception: " << e.what() << std::endl;
        m_strand.post(std::bind(completion_handler,
                    connect_result::FATAL_CONNECT_ERROR));
        m_state = state::DISCONNECTED;
    }
}

void p2u::nntp::connection::async_connect(connect_handler handler)
{
    if (m_state == state::DISCONNECTED)
    {
        boost::asio::spawn(m_strand,
                std::bind(&connection::do_connect, this,
                    handler, std::placeholders::_1));
    }
}


void p2u::nntp::connection::do_post(const std::shared_ptr<article>& message,
                                    post_handler handler,
                                    boost::asio::yield_context yield)
{
    try
    {
        busy_state_lock _lck{m_state};


        write(boost::asio::buffer(protocol::POST), yield);

        std::string line = read_line(yield);

        if (line[0] == '4')
        {
            m_strand.post(std::bind(handler, post_result::POSTING_NOT_PERMITTED));
            return;
        }

        // Start posting

        std::ostringstream header;
        message->get_header().write_to(header);

        std::string actual_header = header.str();

        std::vector<boost::asio::const_buffer> send_parts;

        // Three fixed parts + payload pieces
        send_parts.reserve(3 + message->get_payload_pieces());

        send_parts.push_back(boost::asio::buffer(actual_header));
        send_parts.push_back(boost::asio::buffer(protocol::CRLF));
        message->write_payload_asio_buffers(std::back_inserter(send_parts));
        send_parts.push_back(boost::asio::buffer(protocol::MESSAGE_TERM));

        //std::array<boost::asio::const_buffer, 4> send_parts = {
        //        boost::asio::buffer(actual_header), // Header
        //        boost::asio::buffer(protocol::CRLF), // Followed by an empty line
        //        boost::asio::buffer(message->article_payload), // Payload
        //        boost::asio::buffer(protocol::MESSAGE_TERM) // Followed by terminator
        //      };

        write(send_parts, yield);


        line = read_line(yield);

        if (line[0] == '2')
        {
            // Post successful
            std::cout << "Connection: " << m_sock.native_handle()
                << ": Article sent: "
                << message->get_header().subject << std::endl;

            m_strand.post(std::bind(handler, post_result::POST_SUCCESS));
        } else
        {
            m_strand.post(std::bind(handler, post_result::POST_FAILURE));
        }

    }
    catch (std::exception &e)
    {
        std::cout << "Caught exception: " << e.what() << std::endl;
        close();
        m_strand.post(std::bind(handler,
                    post_result::POST_FAILURE_CONNECTION_ERROR));
    }
}

bool p2u::nntp::connection::async_post(const std::shared_ptr<article>& message,
                                       post_handler handler)
{
    if (m_state != state::CONNECTED_AND_AUTHENTICATED)
    {
        return false;
    }

    boost::asio::spawn(m_strand,
            std::bind(&connection::do_post,
                            this, message, handler, std::placeholders::_1));
    return true;
}

void p2u::nntp::connection::send_stat_cmd(const std::string& mid,
                                          boost::asio::yield_context& yield)
{
    std::array<boost::asio::const_buffer, 3> parts = {
        boost::asio::buffer(protocol::STAT),
        boost::asio::buffer(mid),
        boost::asio::buffer(protocol::CRLF)
    };

    write(parts, yield);
}

void p2u::nntp::connection::do_stat(const std::string& mid,
                                    stat_handler handler,
                                    boost::asio::yield_context yield)
{
    try
    {
        send_stat_cmd(mid, yield);
        std::string line = read_line(yield);

        if (line[0] == '2')
        {
            // 223 article exists
            m_strand.post(std::bind(handler, stat_result::ARTICLE_EXISTS));
        }
        else
        {
            // 430 no article with that message-id
            m_strand.post(std::bind(handler, stat_result::INVALID_ARTICLE));
        }
    }
    catch (std::exception& e)
    {
        m_strand.post(std::bind(handler, stat_result::CONNECTION_ERROR));
    }
}

bool p2u::nntp::connection::async_stat(const std::string& mid,
                                       stat_handler handler)
{
    if (m_state != state::CONNECTED_AND_AUTHENTICATED)
    {
        return false;
    }

    boost::asio::spawn(m_strand, std::bind(&connection::do_stat, this, mid,
                handler, std::placeholders::_1));
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

    }

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

