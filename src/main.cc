#include <iostream>
#include <boost/asio.hpp>
#include <memory>

#include "nntp/connection_info.hpp"
#include "nntp/connection.hpp"
#include "nntp/message.hpp"


int main(int argc, const char* argv[])
{
    if (argc < 5)
    {
        std::cout << "Usage: " << argv[0] << " address port username password" << std::endl;
        return 1;
    }

    boost::asio::io_service io_service;

    boost::asio::deadline_timer timer(io_service);

    timer.expires_from_now(boost::posix_time::seconds(1));

    std::function<void(const boost::system::error_code& ec)> tick = [&timer, &tick](const boost::system::error_code& ec)
    {
        std::cout << "Timer async tick!" << std::endl;
        timer.expires_from_now(boost::posix_time::seconds(1));
        timer.async_wait(tick);
    };

    timer.async_wait(tick);

    p2u::nntp::connection_info conn_info;

    conn_info.username = argv[3];
    conn_info.password = argv[4];
    conn_info.serveraddr = argv[1];
    conn_info.port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    conn_info.tls = false;


    p2u::nntp::connection nntp_connection{io_service, conn_info};
    nntp_connection.async_connect([&nntp_connection]()
            {
                std::cout << "connected yay!" << std::endl;
                auto test_post = std::make_shared<p2u::nntp::article>();
                test_post->article_header = {"test_post@testpost.com", {"misc.test"}, "Test post please ignore", {}};
                std::string content = "test content";
                test_post->article_payload = std::vector<char>(content.begin(), content.end());

                nntp_connection.async_post(test_post, [](p2u::nntp::post_result result)
                    {
                        if (result == p2u::nntp::post_result::POST_SUCCESS)
                        {
                            std::cout << "successfully posted!" << std::endl;
                        } else
                        {
                            std::cout << "failed!" << std::endl;
                        }
                    });

            });
    io_service.run();
    return 0;
}
