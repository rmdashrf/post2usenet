#include <iostream>
#include <fstream>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#include "nntp/message.hpp"
#include "nntp/connection_info.hpp"
#include "nntp/usenet.hpp"



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

p2u::nntp::connection_info get_conninfo_from_args(int argc, const char* argv[])
{
    p2u::nntp::connection_info conn_info;

    conn_info.username = argv[3];
    conn_info.password = argv[4];
    conn_info.serveraddr = argv[1];
    conn_info.port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    conn_info.tls = std::stol(argv[5]) > 0;

    return conn_info;
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

    p2u::nntp::connection_info conn_info = get_conninfo_from_args(argc, argv);

    size_t payload_size = argc >= 9 ? std::stoul(argv[8]) : 0;

    int num_articles = argc >= 8 ? std::stoi(argv[7]) : 100;
    size_t num_connections = argc >= 7 ? std::stol(argv[6]) : 15;

    std::cout << "Posting " << num_articles << " each with payload size "
        << payload_size << " among " << num_connections << " connections."
        << std::endl;

    p2u::nntp::usenet poster{1}; // Use 1 IO thread
    // TODO: Race condition here
    poster.add_connections(conn_info, num_connections);
    poster.start();

    size_t total_payload_bytes = 0;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_articles; ++i)
    {
        auto article = std::make_shared<p2u::nntp::article>();
        article->article_header = {"newsposter@tester.com", {"misc.test"}, get_subject(run_nonce, i+1, num_articles), {}};
        article->article_payload = get_body(run_nonce, i+1, num_articles, payload_size);
        poster.enqueue(article);
        total_payload_bytes += article->article_payload.size();
    }

    poster.join();
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = end - start;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto avg_speed = total_payload_bytes / seconds;
    std::cout << "Finished sending " << std::dec << total_payload_bytes << " in " << seconds << " seconds for an average speed of " << avg_speed/1024.0 << " KB/s" << std::endl;
    return 0;
}
