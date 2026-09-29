#include <iostream>
#include "config.hpp"

int main() {
  std::cout << "Wallpaper Daemon - C++\n";
  std::cout << "Starting application...\n";

  Config config = create_default_config();

  std::cout << "wallpaper directory: " << config.wallpaper_directory << '\n';
  std::cout << "interval seconds: " << config.interval_seconds << '\n';

  return 0;
}