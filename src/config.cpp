#include "config.hpp"

Config create_default_config(){
    Config config;

    config.wallpaper_directory = "./assets";
    config.interval_seconds = 60;

    return config;
};