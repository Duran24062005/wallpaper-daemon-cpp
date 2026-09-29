#include "../include/scanner.hpp"

#include <filesystem>

std::vector<std::string> scan_wallpapers(const std::string& directory) {
    std::vector<std::string> wallpapers;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file())
        {
            /* code */
            continue;
        }
        
        std::string extensions = entry.path().extension().string();

        if (
            extensions == ".jpg" ||
            extensions == ".jpeg" ||
            extensions == ".png" ||
            extensions == ".webp"
        ){
            /* code */
            wallpapers.push_back(entry.path().string());
        }
        
    }

    return wallpapers;

};