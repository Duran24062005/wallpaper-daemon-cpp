#include "../include/selector.hpp"

#include <stdexcept>

std::string select_wallpaper(
    const std::vector<std::string>& wallpapers,
    std::size_t index
){
    if (wallpapers.empty())
    {
        /* code */
        throw std::runtime_error("No wallpapers available.");
    }

    if (index >= wallpapers.size())
    {
        /* code */
        throw std::runtime_error("Wallpaper index out of range.");
    }
    
    return wallpapers[index];
};