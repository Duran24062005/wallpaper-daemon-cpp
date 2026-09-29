#pragma once

#include <string>

struct Config
{
    /* data */
    std::string wallpaper_directory;
    int interval_seconds;
};

Config create_default_config();