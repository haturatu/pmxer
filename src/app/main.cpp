#include "../ui/MainWindow.hpp"

#include <filesystem>

int main(int argc, char **argv) {
    const std::filesystem::path path = argc > 1 ? argv[1] : std::filesystem::path{};
    return pmxer::runApplication(argc > 1 ? &path : nullptr);
}

