#include <iostream>

#include "../include/config.hpp"
#include "../include/scanner.hpp"
#include "../include/selector.hpp"

int main() {
  std::cout << "Wallpaper Daemon - C++\n";
  std::cout << "Starting application...\n";

  Config config = create_default_config();

  std::cout << "wallpaper directory: " << config.wallpaper_directory << '\n';
  std::cout << "interval seconds: " << config.interval_seconds << '\n';

  // Wallpaper vector created
  std::vector<std::string> wallpaper_vector = scan_wallpapers(config.wallpaper_directory);

  std::cout << "Wallpapers found: " << wallpaper_vector.size() << '\n';
  
  std::string selected_wallpaper = select_wallpaper(wallpaper_vector, 0);
  
  std::cout << "Selected wallpaper: " << selected_wallpaper << '\n';
  
  return 0;
}