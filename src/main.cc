#include <iostream>
#include <fstream>
#include <iomanip>
#include <random>
#include <chrono>
#include "program_config.hpp"
#include "fileset.hpp"
#include "nntp/message.hpp"
#include "nntp/usenet.hpp"
#include <boost/algorithm/string/replace.hpp>

const int NONCE_LENGTH = 16;
using msgid_exceptions_map = std::map<filepiece_key, std::string>;

static std::mt19937 rng{static_cast<std::mt19937::result_type>(
            std::chrono::system_clock::now().time_since_epoch().count())};

static std::string get_run_nonce(size_t length)
{
    static const char choices[] = "abcdefghijklmnopqrstuvwxyz1234567890";
    std::ostringstream stream;

    for (size_t i = 0; i < length; ++i)
    {
        stream << choices[rng() % (sizeof(choices) - 1)];
    }

    return stream.str();
}

std::string ghetto_xml_escape(const std::string& str)
{
    auto escaped = str;
    boost::algorithm::replace_all(escaped, "\"", "&quot;");
    boost::algorithm::replace_all(escaped, "'", "&apos;");
    boost::algorithm::replace_all(escaped, "<", "&lt;");
    boost::algorithm::replace_all(escaped, ">", "&gt;");
    boost::algorithm::replace_all(escaped, ">", "&amp;");
    return escaped;
}

using piece_size_map = std::map<size_t, size_t>;

void write_nzb(std::ostream& stream, const fileset& files, const prog_config& cfg, const std::vector<piece_size_map>& piece_sizes, const std::string& nonce, const msgid_exceptions_map& exceptions)
{
    auto epoch_time = std::chrono::system_clock::now().time_since_epoch().count();

    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    stream << "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.1//END\" \"http://www.newzbin.com/DTD/nzb/nzb-1.1.dtd\">" << std::endl;
    stream << "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">" << std::endl;
    stream << std::endl;

    for (size_t i = 0; i < files.get_num_files(); ++i)
    {
        stream << "<file poster=\"" << ghetto_xml_escape(cfg.from)
            << "\" date=\"" << epoch_time
            << "\" subject=\""
            << ghetto_xml_escape(files.get_usenet_subject(cfg.subject, i, 0)) << "\">" << std::endl;

        stream << "<groups>" << std::endl;
        for (const auto& group : cfg.groups)
        {
            stream << "<group>" << group << "</group>" << std::endl;
        }
        stream << "</groups>" << std::endl;

        stream << "<segments>" << std::endl;
        for (size_t pieceIndex = 0; pieceIndex < files.get_num_pieces(i); ++pieceIndex)
        {
            auto it = piece_sizes[i].find(pieceIndex);

            auto exception_it = exceptions.find({i, pieceIndex});
            const auto& actual_nonce = exception_it == exceptions.end() ? nonce : exception_it->second;
            auto msg_id = fileset::get_usenet_message_id(actual_nonce, i, pieceIndex);
            boost::algorithm::replace_all(msg_id, "<", "");
            boost::algorithm::replace_all(msg_id, ">", "");

            stream << "<segment bytes=\"" << it->second
                << "\" number=\"" << pieceIndex + 1
                << "\">" << msg_id
                << "</segment>" << std::endl;
        }

        stream << "</segments>" << std::endl;
        stream << "</file>" << std::endl;
    }

    stream << "</nzb>" << std::endl;
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

    if (!cfg.nzboutput.empty())
    {
        fs::path p{cfg.nzboutput};
        if (boost::filesystem::is_directory(p))
        {
            fs::path file_out{cfg.subject + ".nzb"};
            cfg.nzboutput = (p / file_out).generic_string();
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
            std::cerr << "[WARN] Ignoring " << path << " since it is neither a file nor a directory!" << std::endl;
        }
    }


    p2u::nntp::usenet usenet{cfg.io_threads, cfg.queue_size};
    usenet.set_operation_timeout(cfg.operation_timeout);
    for (const auto& p : cfg.servers)
    {
        usenet.add_connections(p.first, p.second);
    }

    std::string run_nonce = get_run_nonce(NONCE_LENGTH);

    size_t num_total_files = postitems.get_num_files();
    size_t total_parts = postitems.get_total_pieces();
    size_t num_posted = 0;
    uint64_t bytes_posted = 0;

    auto post_start = std::chrono::system_clock::now();

    // TODO: We are only dealing with 1 IO thread right now. Guard this in the future
    msgid_exceptions_map msgid_exceptions;
    std::map<filepiece_key, int> msgid_retries;

    usenet.set_stat_finished_callback([&](const std::string& msgid, p2u::nntp::stat_result result)
            {
                // TODO: Resubmit appropriate piece when stat result fails.
                std::cout << "HEADER CHECK> " << msgid << " - " << (result == p2u::nntp::stat_result::ARTICLE_EXISTS ? "OK" : "FAIL") << std::endl;
            });


    usenet.set_post_failed_callback([&](const std::shared_ptr<p2u::nntp::article>& article)
            {
                auto key = fileset::get_key_from_message_id(article->get_header().msgid);
                auto it = msgid_retries.find(key);
                if (it == msgid_retries.end())
                {
                    auto inserted = msgid_retries.insert(std::make_pair(key, 1));
                    it = inserted.first;
                }
                else
                {
                    it->second++;
                    if (it->second >= 3)
                    {
                        std::cerr << "[FATAL] Too many retries while posting " << article->get_header().msgid << ". Aborting program!" << std::endl;
                        std::ostringstream dumpfile;
                        dumpfile << article->get_header().msgid << ".dump";
                        std::ofstream dump{dumpfile.str().c_str(), std::ofstream::binary};
                        if (!dump.is_open())
                        {
                            std::cerr << "[FATAL] Could not open dump file. Discarding.." << std::endl;
                            std::exit(1);
                        }

                        std::ostringstream header;
                        article->get_header().write_to(header);

                        std::string headerstr = header.str();
                        dumpfile.write(headerstr.c_str(), headerstr.length());
                        dumpfile.write("\r\n", 2);

                        std::vector<boost::asio::const_buffer> buffers;
                        article->write_payload_asio_buffers(std::back_inserter(buffers));
                        assert(buffers.size() > 0);
                        for (auto& p : buffers)
                        {
                            const char* buf = boost::asio::buffer_cast<const char*>(p);
                            size_t bufsize = boost::asio::buffer_size(p);
                            dumpfile.write(buf, bufsize);
                        }

                        dumpfile.write("\r\n.\r\n", 5);
                        dumpfile.flush();
                        std::exit(1);
                    }
                }

                std::cerr << "[WARN] Posting " << article->get_header().subject << " failed. Retry #" << it->second << std::endl;
                // Try changing the message id and restarting
                msgid_exceptions[key] = get_run_nonce(NONCE_LENGTH);
                const_cast<p2u::nntp::header&>(article->get_header()).msgid =
                    fileset::get_usenet_message_id(msgid_exceptions[key], key.file_index, key.piece_index);
                usenet.enqueue_post(article, true);
                std::cerr << "[INFO] Requeued post " << article->get_header().subject << " with message id " << article->get_header().msgid << std::endl;
            });

    usenet.set_post_finished_callback([&](const std::shared_ptr<p2u::nntp::article>& article)
            {
                ++num_posted;
                bytes_posted += article->get_payload_size();

                auto seconds_elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - post_start).count();
                uint64_t speed_kb = 0;
                if (seconds_elapsed != 0)
                {
                    speed_kb = (bytes_posted / seconds_elapsed) / 1024;
                }

                int percentage_complete = static_cast<int>((static_cast<float>(num_posted) / total_parts) * 100);

                size_t pieces_remaining = total_parts - num_posted;
                if (percentage_complete > 100 || pieces_remaining == 0)
                {
                    percentage_complete = 100;
                }
                std::cout << "STATUS> " << percentage_complete << "% - Pieces Remaining: " << pieces_remaining << " - Average Speed: " << speed_kb << " KB/s" << std::endl;
            });

    usenet.start();

    std::vector<piece_size_map> piece_sizes;

    for (size_t fileIndex = 0; fileIndex < num_total_files; ++fileIndex)
    {
        size_t num_pieces = postitems.get_num_pieces(fileIndex);
        auto cur_file_name = postitems.get_file_name(fileIndex);
        piece_sizes.emplace_back();

        for (size_t pieceIndex = 0; pieceIndex < num_pieces; ++pieceIndex)
        {
            // Body
            auto chunk = postitems.get_chunk(fileIndex, pieceIndex);

            piece_sizes[fileIndex][pieceIndex] = chunk.size();

            // Header
            p2u::nntp::header header;

            header.from = cfg.from;
            header.subject = postitems.get_usenet_subject(cfg.subject, fileIndex, pieceIndex);
            header.msgid = postitems.get_usenet_message_id(run_nonce, fileIndex, pieceIndex);
            std::copy(cfg.groups.begin(), cfg.groups.end(), std::back_inserter(header.newsgroups));
            header.additional.push_back({"X-Newsposter", "post2usenet"});

            auto article = std::make_shared<p2u::nntp::article>(header);
            article->add_payload_piece(std::move(chunk));
            usenet.enqueue_post(article);
        }
    }

    // TODO: Somehow figure out to validate the right posts. (When we retry, we generate a new message id)
    if (cfg.validate_posts)
    {
        for (size_t fileIndex = 0; fileIndex < num_total_files; ++fileIndex)
        {
            size_t num_pieces = postitems.get_num_pieces(fileIndex);
            for (size_t pieceIndex = 0; pieceIndex < num_pieces; ++pieceIndex)
            {
                usenet.enqueue_stat(postitems.get_usenet_message_id(run_nonce, fileIndex, pieceIndex));
            }
        }
    }

    usenet.stop();
    usenet.join();

    if (usenet.get_queue_size() == 0)
    {
        if (!cfg.nzboutput.empty())
        {
            std::ofstream nzboutstream;
            nzboutstream.open(cfg.nzboutput.c_str());
            if (!nzboutstream.is_open())
            {
                std::cout << "ERROR: Cannot open " << cfg.nzboutput << " for writing. Nzb output discarded." << std::endl;
            }
            else
            {
                write_nzb(nzboutstream, postitems, cfg, piece_sizes, run_nonce, msgid_exceptions);
            }
        }
        return 0;
    }
    else
    {
        std::cout << "ERROR: Workers died when there was still work for them to do!. " << std::endl;
        return 1;
    }


}

