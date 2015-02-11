#ifndef PROGRAM_CONFIG_HPP_
#define PROGRAM_CONFIG_HPP_

#include <vector>
#include <string>

#include "nntp/connection_info.hpp"

struct prog_config
{
    std::string from;
    std::vector<std::pair<p2u::nntp::connection_info, int>> servers;
    std::string subject;
    size_t article_size;
    size_t io_threads;
    bool validate_posts;
    bool raw;
};

bool load_program_config(int argc, const char* argv[], prog_config& cfg);

#endif
