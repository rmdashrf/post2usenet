#include "message.hpp"
#include <vector>
#include <iostream>

using namespace p2u::nntp;
int main()
{
    std::vector<char> test1{'h', 'e', 'l', 'l', 'o'};
    std::vector<char> test2{'w', 'o', 'r', 'l', 'd'};

    header h;
    article a(std::move(h));
    a.add_payload_piece(std::move(test1));
    a.add_payload_piece(std::move(test2));

    std::cout << a.get_payload_pieces() << std::endl;
    std::vector<boost::asio::const_buffer> buffers;
    a.write_payload_asio_buffers(std::back_inserter(buffers));

    return 0;
}
