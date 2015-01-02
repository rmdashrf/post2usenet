#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#include <array>
#include <deque>

#include "nntp/connection_info.hpp"
#include "nntp/connection.hpp"
#include "nntp/message.hpp"

std::string get_run_nonce()
{
    std::string ret;
    char buf[4];

    ret.reserve(sizeof(buf) * 2);

    std::ifstream rnd("/dev/urandom", std::ifstream::binary);
    rnd.read(buf, sizeof(buf));

    boost::algorithm::hex(std::begin(buf), std::end(buf), std::back_inserter(ret));
    return ret;
}

std::string get_subject(const std::string& run_nonce, int count, int total)
{
    std::ostringstream stream;
    stream <<  "Test Run: [" << run_nonce << "] (" << count << "/" << total << ")";
    return stream.str();
}

p2u::nntp::message_payload get_body(const std::string& run_nonce, int count, int total)
{
    p2u::nntp::message_payload ret;
    std::ostringstream stream;
    stream << "Testing posting\r\n"
           << "Run: " << count << "/" << total << "\r\n"
           << "Nonce: " << run_nonce;
    const auto& body = stream.str();
    ret = p2u::nntp::message_payload(body.begin(), body.end());
    return ret;
}

void post_next_article(p2u::nntp::connection* conn,
                       std::deque<std::shared_ptr<p2u::nntp::article>>& articles)
{
    std::cout << "[entering post_next_article]" << std::endl;
    std::cout << "Post next article on connection: " << std::hex << conn << std::endl;

    if (articles.size() < 1)
    {
        std::cout << "[exiting post_next_article]" << std::endl;
        conn->close();
    }

    auto article = articles.front();
    articles.pop_front();
    std::cout << "Trying to post article: " << article->article_header.subject << std::endl;

    conn->get_io_service().post([conn, &articles, article]() {
        if (!conn->async_post(article, [article, conn, &articles](p2u::nntp::post_result ec)
                {
                    std::cout << "article with subject "
                    << article->article_header.subject
                    << " posted " << ((ec == p2u::nntp::post_result::POST_SUCCESS) ? " successfully " : " unsucessfully") << std::endl;

                    if (articles.size() > 0)
                    {
                        conn->get_io_service().post(std::bind(&post_next_article, conn, std::ref(articles)));
                    }
                }))
                {
                    std::cout << "Article " << article->article_header.subject << " failed to post" << std::endl;
                }
    });
    std::cout << "[exiting post_next_article]" << std::endl;
}


int main(int argc, const char* argv[])
{
    if (argc < 5)
    {
        std::cout << "Usage: " << argv[0] << " address port username password" << std::endl;
        return 1;
    }

    std::string run_nonce = get_run_nonce();
    std::cout << "Run nonce: " << run_nonce << std::endl;
    boost::asio::io_service io_service;

    boost::asio::deadline_timer timer(io_service);

    timer.expires_from_now(boost::posix_time::seconds(1));

    std::function<void(const boost::system::error_code& ec)> tick = [&timer, &tick](const boost::system::error_code& ec)
    {
        std::cout << "Timer async tick!" << std::endl;
        timer.expires_from_now(boost::posix_time::seconds(1));
        timer.async_wait(tick);
    };

    //timer.async_wait(tick);

    p2u::nntp::connection_info conn_info;

    conn_info.username = argv[3];
    conn_info.password = argv[4];
    conn_info.serveraddr = argv[1];
    conn_info.port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    conn_info.tls = false;


    std::vector<std::unique_ptr<p2u::nntp::connection>> connections;
    std::deque<std::shared_ptr<p2u::nntp::article>> articles;

    const int NUM_ARTICLES = 30;
    const int NUM_CONNECTIONS = 15;

    for (int i = 0; i < NUM_ARTICLES; ++i)
    {
        auto article = std::make_shared<p2u::nntp::article>();
        article->article_header = {"newsposter@tester.com", {"misc.test"}, get_subject(run_nonce, i+1, NUM_ARTICLES), {}};
        article->article_payload = get_body(run_nonce, i+1, NUM_ARTICLES);
        articles.push_back(article);
    }

    for (int i = 0; i < NUM_CONNECTIONS; ++i)
    {
        connections.emplace_back(new p2u::nntp::connection(io_service, conn_info));
        const auto conn = connections.back().get();
        std::cout << "Starting async connect on connection : " << std::hex << conn << std::dec << std::endl;
        conn->async_connect([&io_service, conn, &articles]()
                {
                    std::cout << "[[Entering on_connected callback]]" << std::endl;
                    io_service.post(std::bind(&post_next_article, conn, std::ref(articles)));
                    std::cout << "[[Exiting on_connected callback]]" << std::endl;
                });
    }

    io_service.run();
    return 0;
}
