#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <algorithm>
#include "program_config.hpp"


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

static void read_optional_string(boost::property_tree::ptree& ptree, const std::string& key, std::string& dst)
{
    auto it = ptree.find(key);
    if (it == ptree.not_found())
        return;
    dst = it->second.get_value<std::string>();
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
    read_numeric_value(global_section, "ArticleQueueSize", cfg.queue_size);
    read_numeric_value(global_section, "OperationTimeout", cfg.operation_timeout);
    read_optional_string(global_section, "MsgIdDomain", cfg.msgiddomain);

    if (cfg.msgiddomain.empty()) {
        cfg.msgiddomain = "post2usenet";
    }

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

    if (vm.count("articlesize"))
    {
        cfg.article_size = vm["articlesize"].as<int>();
    }

    if (vm.count("subject"))
    {
        cfg.subject = vm["subject"].as<std::string>();
    }

    cfg.validate_posts = vm.count("validate");
    cfg.raw = vm["raw"].as<bool>();

    auto& files = vm["file"].as<std::vector<std::string>>();

    std::transform(files.begin(), files.end(), std::back_inserter(cfg.files),
            [](const std::string& p)
            {
                return boost::filesystem::path(p);
            });

    if (vm.count("group"))
    {
        cfg.groups = vm["group"].as<std::vector<std::string>>();
    }

    if (vm.count("output"))
    {
        cfg.nzboutput = vm["output"].as<std::string>();
    }
}

bool load_program_config(int argc, const char* argv[], prog_config& cfg)
{
    po::options_description hidden{"So secret!"};
    hidden.add_options()
        ("iothreads", po::value<int>()->default_value(1), "Number of IO threads to use. 1 IO thread is usually fine.");

    po::options_description cli{"Command line arguments"};

    cli.add_options()
        ("help,h", "Show this message")
        ("articlesize,a", po::value<int>(), "Size in bytes of each article")
        ("raw,r", po::value<bool>()->default_value(true), "Raw post mode. Emulates GoPostStuff, newsmangler, etc.")
        ("validate,v", "Validate articles after post. Issues STAT command to ensure article was properly posted. Repost articles if bad.")
        ("subject,s", po::value<std::string>(), "Subject of the post. By default, will be set to the folder name if input is a folder, otherwise will be set to the filename")
        ("config,c", po::value<std::string>(), "Specifies configuration file path")
        ("output,o", po::value<std::string>(), "Specifies output NZB file/directory")
        ("group,g", po::value<std::vector<std::string>>(), "Groups to post to")
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
            return false;
        }

        po::notify(vm);
    }
    catch (po::error& e)
    {
        std::cout << cli << std::endl;
        return false;
    }

    if (vm.count("file") < 1)
    {
        std::cout << "Missing files" << std::endl;
        return false;
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
        return false;
    }

    try
    {
        read_configuration_file(pathToConfig, cfg);

        if (cfg.servers.size() < 1)
        {
            std::cout << "Need at least one server block! (Where else am I supposed to post?)" << std::endl;
            return false;
        }

        read_cmdline_args(vm, cfg);

        if (cfg.groups.size() < 1)
        {
            std::cout << "Need at least one group to post to!" << std::endl;
            return false;
        }

        bool good = true;
        for (const auto& path : cfg.files)
        {
            if (!boost::filesystem::exists(path))
            {
                std::cout << "ERROR: Path " << path << " does not exist!" << std::endl;
                good = false;
            }
        }

        return good;
    }
    catch (std::exception& e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

