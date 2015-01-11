#include "connection_info.hpp"

bool p2u::nntp::operator==(const connection_info& first,
                           const connection_info& second)
{
    return first.username == second.username &&
        first.password == second.password &&
        first.serveraddr == second.serveraddr &&
        first.port == second.port &&
        first.tls == second.tls;
}
