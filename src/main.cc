#include <iostream>
#include <fstream>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "nntp/message.hpp"
#include "nntp/connection_info.hpp"
#include "nntp/usenet.hpp"

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


static void read_nonzero_string(boost::property_tree::ptree& ptree,
                               const std::string& key,
                               std::string& dst)
{
    auto it = ptree.find(key);
    if (it == ptree.not_found())
    {
        throw std::runtime_error{std::string("Key not found: ") + key};
    }

    dst = it->second.get_value<std::string>();
    if (dst.size() < 1)
    {
        throw std::runtime_error{std::string("Must not be empty: ") + key};
    }
}

static void read_boolean_value(boost::property_tree::ptree& ptree,
                               const std::string& key,
                               bool& dst)
{
    // First try to read it as a string
    std::string str;
    read_nonzero_string(ptree, key, str);

    dst = (boost::algorithm::iequals(str, "yes") ||
            boost::algorithm::iequals(str, "true")) ? true : false;

}

template <class T>
static void read_numeric_value(boost::property_tree::ptree& ptree,
                               const std::string& key,
                               T& dst)
{
    auto it = ptree.find(key);
    if (it == ptree.not_found())
    {
        throw std::runtime_error{std::string("Key not found: ") + key};
    }

    dst = it->second.get_value<T>();
}


static void read_server_configuration(boost::property_tree::ptree& tree_node,
                                      prog_config& cfg)
{
    p2u::nntp::connection_info conn;
    read_nonzero_string(tree_node, "Address", conn.serveraddr);
    read_numeric_value(tree_node, "Port", conn.port);

    // TODO: Support usenet connections without username and password
    read_nonzero_string(tree_node, "Username", conn.username);
    read_nonzero_string(tree_node, "Password", conn.password);
    read_boolean_value(tree_node, "TLS", conn.tls);

    // Num Connections
    int num_connections;
    read_numeric_value(tree_node, "Connections", num_connections);

    cfg.servers.push_back(std::make_pair(std::move(conn), num_connections));
}

static void read_configuration_file(const boost::filesystem::path& path,
                                    prog_config& cfg)
{
    using boost::property_tree::ptree;

    ptree pt;
    boost::property_tree::read_ini(path.string(), pt);

    // Global settings
    auto it = pt.find("global");
    if (it == pt.not_found())
    {
        throw std::runtime_error{"Missing global section in configuration file"};
    }

    auto& global_section = it->second;

    read_nonzero_string(global_section, "From", cfg.from);
    read_numeric_value(global_section, "ArticleSize", cfg.article_size);

    // Read server configurations
    for (auto& section : pt)
    {
        if (boost::algorithm::starts_with(section.first, "Server"))
        {
            read_server_configuration(section.second, cfg);
        }
    }
}


namespace po = boost::program_options;

static void read_cmdline_args(const po::variables_map& vm, prog_config& cfg)
{
    cfg.io_threads = vm["iothreads"].as<int>();
    cfg.article_size = vm["articlesize"].as<int>();
    cfg.subject = vm["subject"].as<std::string>();
    cfg.validate_posts = vm["validate"].as<bool>();

}

int main(int argc, const char* argv[])
{

    po::options_description hidden{"So secret!"};
    hidden.add_options()
        ("iothreads", po::value<int>()->default_value(1), "Number of IO threads to use. 1 IO thread is usually fine.");

    po::options_description cli{"Command line arguments"};

    cli.add_options()
        ("help,h", "Show this message")
        ("articlesize,a", po::value<int>()->default_value(750000), "Size in bytes of each article")
        ("raw,r", po::value<bool>()->default_value(true), "Raw post mode. Emulates GoPostStuff, newsmangler, etc.")
        ("validate,v", po::value<bool>()->default_value(false), "Validate articles after post. Issues STAT command to ensure article was properly posted. Repost articles if bad.")
        ("subject,s", po::value<std::string>()->required(), "Subject of the post. By default, will be set to the folder name if input is a folder, otherwise will be set to the filename")
        ("config,c", po::value<std::string>(), "Specifies configuration file path")
        ("file", po::value<std::vector<std::string>>()->required(), "File or directory to post");

    po::positional_options_description positionalopts;
    positionalopts.add("file", -1);

    po::options_description all_cli;
    all_cli.add(hidden).add(cli);

    po::variables_map vm;
    try
    {
        po::store(po::command_line_parser(argc, argv).options(all_cli).positional(positionalopts).run(), vm);

        if (vm.count("help"))
        {
            std::cout << "post2usenet " << std::endl << cli << std::endl;
            return 0;
        }

        po::notify(vm);
    }
    catch (po::error& e)
    {
        std::cout << cli << std::endl;
        return 1;
    }

    if (vm.count("file") < 1)
    {
        std::cout << "Missing files" << std::endl;
        return 1;
    }

    boost::filesystem::path pathToConfig;
    if (vm.count("config") < 1)
    {
        pathToConfig = getenv("HOME");
        pathToConfig /= ".post2usenet.conf";
    }
    else
    {
        pathToConfig = vm["config"].as<std::string>();
    }

    if (!boost::filesystem::exists(pathToConfig))
    {
        std::cout << "Invalid path to configuration file " << pathToConfig << std::endl;
        return 1;
    }

    prog_config cfg;
    try
    {
        read_configuration_file(pathToConfig, cfg);
        read_cmdline_args(vm, cfg);
        std::cout << cfg.from << std::endl;
    }
    catch (std::exception& e)
    {
        std::cout << e.what() << std::endl;
        return 1;
    }

    const auto& files = vm["file"].as<std::vector<std::string>>();
    for (const auto& f : files)
    {
        std::cout << "File: " << f << std::endl;
    }
    std::cout << "Number of io threads: " << vm["iothreads"].as<int>() << std::endl;

    return 0;
}
