// MIT License
//
// Copyright (c) 2018 Michal Siedlaczek
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//! \file irk.cpp
//! \author Michal Siedlaczek
//! \copyright MIT License

#include <CLI/CLI.hpp>
#include <cstdlib>

const std::string CommandPart = "part";
const std::string CommandWarc = "warc";

int main(int argc, char** argv)
{
    struct {
        std::size_t padding_width = 4;
        std::string output;
        std::vector<std::string> input_files;
        std::size_t limit;
    } args;

    CLI::App app{"irk: Command Linie IRKit tools"};
    app.set_help_flag();
    auto part = app.add_subcommand(
                       "part", "Partition a text file by line number.")
                    ->prefix_command();
    auto warc = app.add_subcommand("warc", "Read and parse WARC collections.")
                    ->prefix_command();

    CLI11_PARSE(app, argc, argv);

    if (!system(NULL)) exit(EXIT_FAILURE);

    std::ostringstream command;
    command << "irk-";
    if (*part) command << CommandPart;
    else if (*warc) command << CommandWarc;
    else {
        std::cout << app.help() << std::endl;
        return 1;
    }

    for (const std::string& arg : app.remaining(true)) {
        command << " " << arg;
    }

    int result = system(command.str().c_str());
    return result;
}

