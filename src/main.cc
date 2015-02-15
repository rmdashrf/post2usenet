#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include "program_config.hpp"
#include "fileset.hpp"
#include "nntp/message.hpp"
#include "nntp/usenet.hpp"

static std::string get_message_id(const std::string& base, size_t fileIndex,
                                  size_t pieceIndex)
{
    std::ostringstream stream;
    stream << "<" << base << "." << fileIndex << "." << pieceIndex << "@post2usenet>";
    return stream.str();
}

static std::string get_run_nonce(size_t length)
{
    static const char choices[] = "abcdefghijklmnopqrstuvwxyz1234567890";

    std::mt19937 rng{static_cast<std::mt19937::result_type>(
            std::chrono::system_clock::now().time_since_epoch().count())};

    std::ostringstream stream;

    for (size_t i = 0; i < length; ++i)
    {
        stream << choices[rng() % (sizeof(choices) - 1)];
    }

    return stream.str();
}

int main(int argc, const char* argv[])
{
    namespace fs = boost::filesystem;

    prog_config cfg;
    if (!load_program_config(argc, argv, cfg))
    {
        return 1;
    }

    if (cfg.subject.empty())
    {
        // If there is only a single file, we just use the file as the subject
        if (cfg.files.size() == 1)
        {
            cfg.subject = cfg.files[0].filename().generic_string();
        }
        else
        {
            std::cout << "You are attempting to posting multiple files. Please specify a subject to identify this grouping of files" << std::endl;
            return 1;
        }
    }

    fileset postitems{cfg.article_size};
    uint64_t total_bytes = 0;

    auto add_postitem = [&postitems, &total_bytes](const boost::filesystem::path& path)
    {
        postitems.add_file(path);
        total_bytes += boost::filesystem::file_size(path);
    };

    for (auto& path : cfg.files)
    {
        if (fs::is_directory(path))
        {
            for (auto it = fs::recursive_directory_iterator{path}; it != fs::recursive_directory_iterator{}; ++it)
            {
                if (fs::is_regular_file(it->path()))
                {
                    add_postitem(it->path());
                }
            }
        }
        else if (fs::is_regular_file(path))
        {
            add_postitem(path);
        }
        else
        {
            std::cout << "WARNING: Ignoring " << path << " since it is neither a file nor a directory!" << std::endl;
        }
    }

    p2u::nntp::usenet usenet{cfg.io_threads, cfg.queue_size};
    for (const auto& p : cfg.servers)
    {
        usenet.add_connections(p.first, p.second);
    }

    usenet.start();
    std::string run_nonce = get_run_nonce(16);
    std::cout << "using run nonce of " << run_nonce << std::endl;

    size_t num_total_files = postitems.get_num_files();
    size_t total_parts = postitems.get_total_pieces();
    size_t num_posted = 0;
    uint64_t bytes_posted = 0;

    auto post_start = std::chrono::system_clock::now();

    usenet.set_post_finished_callback([&](const std::shared_ptr<p2u::nntp::article>& article)
            {
                ++num_posted;
                bytes_posted += article->get_payload_size();

                auto seconds_elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - post_start).count();
                uint64_t speed_kb;
                if (seconds_elapsed != 0)
                {
                    speed_kb = (bytes_posted / seconds_elapsed) / 1024;
                }

                std::cout << "POST FINISH ## " << article->get_header().subject << " ## Pieces Remaining: " << (total_parts - num_posted) << " ## Average Speed: " << speed_kb << " KB/s" << std::endl;

            });

    for (size_t fileIndex = 0; fileIndex < num_total_files; ++fileIndex)
    {
        size_t num_pieces = postitems.get_num_pieces(fileIndex);
        auto cur_file_name = postitems.get_file_name(fileIndex);
        for (size_t pieceIndex = 0; pieceIndex < num_pieces; ++pieceIndex)
        {
            // Article subject name
            std::ostringstream article_subject;
            article_subject << cfg.subject << " [" << fileIndex+1 << "/" << num_total_files << "] - \"" << cur_file_name << "\" yEnc (" << pieceIndex+1 << "/" << num_pieces << ")";

            // Body
            auto chunk = postitems.get_chunk(fileIndex, pieceIndex);


            // Header
            p2u::nntp::header header;
            header.from = cfg.from;
            header.subject = article_subject.str();
            std::copy(cfg.groups.begin(), cfg.groups.end(), std::back_inserter(header.newsgroups));
            header.additional.push_back({"X-Newsposter", "post2usenet"});
            header.additional.push_back({"Message-ID", get_message_id(run_nonce, fileIndex, pieceIndex)});

            auto article = std::make_shared<p2u::nntp::article>(header);
            article->add_payload_piece(std::move(chunk));
            usenet.enqueue_post(article);
        }
    }

    usenet.stop();
    usenet.join();
    std::cout << "Finished posting!" << std::endl;
    return 0;
}

