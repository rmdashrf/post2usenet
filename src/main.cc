#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#include <array>
#include <deque>
#include <mutex>
#include <thread>

#include "nntp/connection_info.hpp"
#include "nntp/connection.hpp"
#include "nntp/message.hpp"


std::mutex qm;
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

const char* LINE = "PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP\r\n";

p2u::nntp::message_payload get_body(const std::string& run_nonce, int count, int total, size_t payload_size)
{
    p2u::nntp::message_payload ret;
    std::string content;
    if (payload_size == 0)
    {
        content.reserve(160);
    } else
    {
        content.reserve(payload_size + 50);
    }

    std::ostringstream stream{content};

    stream << "Testing posting\r\n"
           << "Run: " << count << "/" << total << "\r\n"
           << "Nonce: " << run_nonce << "\r\n";


    if (payload_size != 0)
    {
        while (stream.str().size() < payload_size)
        {
            stream << LINE;
        }
    }

    const auto& body = stream.str();
    ret = p2u::nntp::message_payload(body.begin(), body.end());
    return ret;
}

void post_next_article(p2u::nntp::connection* conn,
                       std::deque<std::shared_ptr<p2u::nntp::article>>& articles)
{
    std::cout << "[entering post_next_article]" << std::endl;
    std::cout << "Post next article on connection: " << std::hex << conn << std::endl;
    std::cout << "articles size: " << articles.size() << std::endl;

    std::unique_lock<std::mutex> lock{qm};
    if (articles.size() < 1)
    {
        std::cout << "No more articles to post. calling close" << std::endl;
        conn->close();
        std::cout << "[exiting post_next_article]" << std::endl;
        return;
    }

    auto article = articles.front();
    articles.pop_front();
    lock.unlock();
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
                    } else
                    {
                        std::cout << "CLosing connection " << conn << std::endl;
                        conn->close();
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
    std::ios_base::sync_with_stdio(false);
    if (argc < 6)
    {
        std::cout << "Usage: " << argv[0] << " address port username password usetls num_connections num_articles payload_size" << std::endl;
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
    conn_info.tls = std::stol(argv[5]) > 0;

    size_t payload_size = argc >= 9 ? std::stoul(argv[8]) : 0;

    std::cout << "Payload size is: " << payload_size << std::endl;

    if (conn_info.tls)
    {
        std::cout << "Using tls!" << std::endl;
    }


    std::vector<std::unique_ptr<p2u::nntp::connection>> connections;
    std::deque<std::shared_ptr<p2u::nntp::article>> articles;

    int num_articles = argc >= 8 ? std::stoi(argv[7]) : 100;
    int num_connections = argc >= 7 ? std::stoi(argv[6]) : 15;

    size_t total_payload_bytes = 0;

    for (int i = 0; i < num_articles; ++i)
    {
        auto article = std::make_shared<p2u::nntp::article>();
        article->article_header = {"newsposter@tester.com", {"misc.test"}, get_subject(run_nonce, i+1, num_articles), {}};
        article->article_payload = get_body(run_nonce, i+1, num_articles, payload_size);
        articles.push_back(article);
        total_payload_bytes += article->article_payload.size();
    }

    for (int i = 0; i < num_connections; ++i)
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

    boost::asio::deadline_timer stats_printer{io_service};
    stats_printer.expires_from_now(boost::posix_time::seconds(2));

    std::function<void(const boost::system::error_code& )> timercb = [&timercb, &stats_printer, &articles](const boost::system::error_code& ec)
            {
                if (!ec) {
                std::cout << articles.size() << " articles left..." << std::endl;
                stats_printer.expires_from_now(boost::posix_time::seconds(2));
                if (articles.size() > 0)
                    stats_printer.async_wait(timercb);
                }
            };

//    stats_printer.async_wait(timercb);

 //   const int NUM_THREADS = 4;
 //   std::vector<std::thread> threads;
 //   for (int i = 0; i < NUM_THREADS; ++i)
 //   {
 //       std::cout << "Spawning thread " << i << std::endl;
 //       threads.emplace_back([&io_service](){ io_service.run();});
 //   }

 //   for (int i = 0; i < NUM_THREADS; ++i)
 //   {
 //       std::cout << "Joining thread " << i << std::endl;
 //       threads[i].join();
 //   }

    auto start = std::chrono::high_resolution_clock::now();
    io_service.run();
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = end - start;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto avg_speed = total_payload_bytes / seconds;
    std::cout << "Finished sending " << std::dec << total_payload_bytes << " in " << seconds << " seconds for an average speed of " << avg_speed/1024.0 << " KB/s" << std::endl;
    return 0;
}
