#include <iostream>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string.hpp>
#include <array>

#include "message.hpp"
#include "connection.hpp"
#include "connection_info.hpp"
#include "../util/asio_helpers.hpp"

p2u::nntp::connection::connection(boost::asio::io_service& io_service,
                                  const connection_info& conn)
    : m_sock {io_service}, m_state {state::DISCONNECTED}, m_conninfo(conn),
      m_strand{io_service}
{
    if (conn.tls)
    {
        initSSL();
    }
}

p2u::nntp::connection::~connection()
{
    std::cout << "connection destructor called " << std::endl;
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
    if (m_sslstream)
    {
        return p2u::asio::async_read_line(*m_sslstream, m_readbuf, yield);
    } else
    {
        return p2u::asio::async_read_line(m_sock, m_readbuf, yield);
    }
}

bool p2u::nntp::connection::do_authenticate(boost::asio::yield_context& yield)
{
    std::cout << "Starting authentication " << std::endl;
    std::string line = read_line(yield);
    std::cout << "[<] " << line << std::endl;
    if (line[0] != '2')
    {
        return false;
    }

    std::ostringstream output;
    output << "AUTHINFO USER " << m_conninfo.username << "\r\n";

    std::cout << "[>] " << output.str() << std::endl;

    write(boost::asio::buffer(output.str()), yield);

    line = read_line(yield);

    std::cout << "[<]" << line << std::endl;

    if (boost::starts_with(line, "381"))
    {
        output.str(std::string());
        output << "AUTHINFO PASS " << m_conninfo.password << "\r\n";
        std::cout << "[>] " << output.str() << std::endl;
        write(boost::asio::buffer(output.str()), yield);
        line = read_line(yield);
        std::cout << "[<] " << line << std::endl;
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
    try
    {
        m_state = state::CONNECTING;
        auto& io_service = m_sock.get_io_service();

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
            std::cout << "TLS enabled. Performing handshake: " << std::endl;
            m_sslstream->async_handshake(m_sslstream->client, yield);
            std::cout << "Finished handshaking " << std::endl;
        }

        if (do_authenticate(yield))
        {
            std::cout << "Authentication successful " << std::endl;
        } else {
            std::cout << "Authentication failed " << std::endl;
        }

        m_state = state::CONNECTED_AND_AUTHENTICATED;
        io_service.post(completion_handler);

    } catch (std::exception& e)
    {
        std::cout << "Caught exception: " << e.what() << std::endl;
        m_state = state::DISCONNECTED;
    }
}

void p2u::nntp::connection::async_connect(connect_handler handler)
{
    std::cout << "[entering async_connect] " << std::endl;
    if (m_state == state::DISCONNECTED)
    {
        boost::asio::spawn(m_strand,
                std::bind(&connection::do_connect, this,
                    handler, std::placeholders::_1));
    }
    std::cout << "[exiting async_connect] " << std::endl;
}


void p2u::nntp::connection::do_post(std::shared_ptr<article> message,
                                       post_handler handler,
                                       boost::asio::yield_context yield)
{
    try {
    busy_state_lock _lck{m_state};

    std::cout << "[Entering do_post]" << std::endl;
    auto& io_service = m_sock.get_io_service();

    std::cout << "[do_post] message: " << message->article_header.subject << std::endl;


    std::cout << "[do_post][>] " << "POST" << std::endl;
    write(boost::asio::buffer(std::string("POST\r\n")), yield);

    std::cout << "[do_post] message: " << message->article_header.subject << std::endl;
    std::string line = read_line(yield);
    std::cout << "[do_post][<] " << line << std::endl;

    if (line[0] == '4')
    {
        // Posting not permitted
        std::cout << "Posting not permitted!" << std::endl;
        io_service.post(std::bind(handler, post_result::POSTING_NOT_PERMITTED));
        return;
    }

    // Start posting

    std::ostringstream header;
    message->article_header.write_to(header);

    std::string actual_header = header.str();

    std::array<boost::asio::const_buffer, 4> send_parts = {
            boost::asio::buffer(actual_header), // Header
            boost::asio::buffer(std::string("\r\n")), // Followed by an empty line
            boost::asio::buffer(message->article_payload), // Payload
            boost::asio::buffer(std::string("\r\n.\r\n")) // Followed by terminator
          };

    size_t bytes_sent = write(send_parts, yield);

    std::cout << "Sent a total of " << std::dec << bytes_sent << " bytes " << std::endl;

    std::cout << "Connection " << this << " about to read the result of POST" << std::endl;
    // Try to read the result
    boost::asio::deadline_timer timer{m_sock.get_io_service()};
    timer.expires_from_now(boost::posix_time::seconds(10));
    timer.async_wait([this](const boost::system::error_code& n)
            {
                if (!n)
                {
                    std::cout << "READ LINE EXPIRED!!!" << std::endl;
                    this->m_sock.cancel();
                }
            });

    line = read_line(yield);
    timer.cancel();
    std::cout << "(Connection: " << this << ")[<] " << line << std::endl;
    if (line[0] == '2')
    {
        // Post successful
        io_service.post(std::bind(handler, post_result::POST_SUCCESS));
    } else
    {
        // TODO: Retry posting
        // Post failure
        std::cout << "Unsuccessful post. ABORT!" << std::endl;
        std::terminate();
        io_service.post(std::bind(handler, post_result::POST_FAILURE));
    }
    std::cout << "[exiting do_post]" << std::endl;
    } catch (std::exception &e)
    {
        std::cout << "Caught exception: " << e.what() << std::endl;
        m_state = state::DISCONNECTED;
    }
}

bool p2u::nntp::connection::async_post(std::shared_ptr<article> message,
                                       post_handler handler)
{
    std::cout << "[entering async_post]" << std::endl;

    if (m_state != state::CONNECTED_AND_AUTHENTICATED)
    {
        std::cout << "STATE NOT CONNECTED AND AUTHENTICATED!" << std::endl;
        std::cout << "[exiting async_post]" << std::endl;
        return false;
    }

    boost::asio::spawn(m_strand,
            std::bind(&connection::do_post,
                            this, message, handler, std::placeholders::_1));
    std::cout << "[exiting async_post]" << std::endl;
    return true;
}


void p2u::nntp::connection::close()
{
    std::cout << "!!!! CONNECTION CLOSE CALLED!" << std::endl;
    m_sock.shutdown(tcp::socket::shutdown_both);
}

boost::asio::io_service& p2u::nntp::connection::get_io_service()
{
    return m_sock.get_io_service();
}
