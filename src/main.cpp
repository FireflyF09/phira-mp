#include "server.h"
#include "l10n.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <cstring>

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [-p PORT]" << std::endl;
    std::cerr << "  -p, --port PORT   Specify the port number (default: 12346)" << std::endl;
    std::cerr << "  -h, --help        Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = 12346;

    // Parse command line args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                i++;
                try {
                    int p = std::stoi(argv[i]);
                    if (p <= 0 || p > 65535) {
                        std::cerr << "Port must be between 1 and 65535" << std::endl;
                        return 1;
                    }
                    port = (uint16_t)p;
                } catch (...) {
                    std::cerr << "Invalid port number: " << argv[i] << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Missing port number after " << arg << std::endl;
                return 1;
            }
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Load localization files
    L10n::instance().load_from_directory("locales");

    // Ignore SIGPIPE (broken pipe on socket write)
    signal(SIGPIPE, SIG_IGN);

    std::cerr << "phira-mp-server (C++ port)" << std::endl;
    std::cerr << "Local Address: [::]:" << port << std::endl;

    try {
        Server server(port);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
