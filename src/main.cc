#include <iostream>
#include "program_config.hpp"
#include "fileset.hpp"
#include <iomanip>

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

    for (auto& path : cfg.files)
    {
        if (fs::is_directory(path))
        {
            for (auto it = fs::recursive_directory_iterator{path}; it != fs::recursive_directory_iterator{}; ++it)
            {
                if (fs::is_regular_file(it->path()))
                {
                    postitems.add_file(it->path());
                }
            }
        }
        else if (fs::is_regular_file(path))
        {
            postitems.add_file(path);
        }
        else
        {
            std::cout << "WARNING: Ignoring " << path << " since it is neither a file nor a directory!" << std::endl;
        }
    }

    size_t num_total_files = postitems.get_num_files();
    for (size_t fileIndex = 0; fileIndex < num_total_files; ++fileIndex)
    {
        size_t num_pieces = postitems.get_num_pieces(fileIndex);
        auto cur_file_name = postitems.get_file_name(fileIndex);
        for (size_t pieceIndex = 0; pieceIndex < num_pieces; ++pieceIndex)
        {
            // Article subject name
            std::ostringstream article_subject;
            article_subject << cfg.subject << " [" << fileIndex+1 << "/" << num_total_files << "] - \"" << cur_file_name << "\" yEnc (" << pieceIndex+1 << "/" << num_pieces << ")";
            std::cout << article_subject.str() << std::endl;

            std::ofstream out;

            std::ostringstream stream;
            stream << cur_file_name << "_yenc." << std::setw(3) << std::setfill('0') << pieceIndex;
            auto outfile = stream.str();

            out.open(outfile.c_str(), std::ofstream::binary);

            auto chunk = postitems.get_chunk(fileIndex, pieceIndex);
            out.write(&chunk[0], chunk.size());
        }
    }
}

