#include "yenc.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iterator>

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " [file.in] " << std::endl;
        return 1;
    }
    std::string infilename{argv[1]};
    std::string outputfilename = infilename + ".yenc";

    std::ifstream somefile(infilename.c_str(), std::ifstream::binary);
    std::ofstream outfile(outputfilename.c_str(), std::ifstream::binary);

    if (!somefile.is_open())
    {
        std::cout << "somefile.in doesn't exist" << std::endl;
        return 1;
    }

    p2u::yenc::encoder yenc_encoder{128};
    for (auto it = std::istreambuf_iterator<char>(somefile); it != std::istreambuf_iterator<char>();)
    {
        it = yenc_encoder(it, std::istreambuf_iterator<char>(), std::ostreambuf_iterator<char>(outfile));
    }

    return 0;
}
