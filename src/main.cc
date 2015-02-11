#include <iostream>

#include "program_config.hpp"

int main(int argc, const char* argv[])
{
    prog_config cfg;
    if (!load_program_config(argc, argv, cfg))
    {
        return 1;
    }

    std::cout << "Ready to roll!" << std::endl;
}

