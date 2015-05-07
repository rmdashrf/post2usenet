#ifndef PROGRAM_CONFIG_HPP_
#define PROGRAM_CONFIG_HPP_

#include <vector>
#include <string>
#include <boost/filesystem.hpp>

#include "nntp/connection_info.hpp"

struct prog_config
{
    std::string from;
    std::vector<std::pair<p2u::nntp::connection_info, int>> servers;
    std::string subject;
    std::string msgiddomain;
    size_t article_size;
    size_t io_threads;
    size_t queue_size;
    int operation_timeout;
    bool validate_posts;
    bool raw;
    std::vector<boost::filesystem::path> files;
    std::vector<std::string> groups;
    std::string nzboutput;
};

bool load_program_config(int argc, const char* argv[], prog_config& cfg);

#endif
