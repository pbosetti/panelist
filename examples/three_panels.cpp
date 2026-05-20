#include <panelist/panelist.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::size_t DefaultSampleCount = 30;

std::size_t parse_sample_count(int argc, char **argv) {
  if (argc < 2) {
    return DefaultSampleCount;
  }

  std::string_view value(argv[1]);
  std::size_t parsed_length = 0;
  const auto parsed = std::stoull(std::string(value), &parsed_length, 10);
  if (parsed_length != value.size()) {
    throw std::invalid_argument("sample count must be a non-negative integer");
  }

  return static_cast<std::size_t>(parsed);
}

} // namespace

int main(int argc, char **argv) {
  try {
    const std::size_t sample_count = parse_sample_count(argc, argv);

    Panelist::Panelist panels(std::cout);
    panels.set_separator("=");
    panels.add_panel(2);
    panels.add_panel();
    panels.add_panel(2);
    panels.layout();

    std::cout << panels[0][1] << "three_panels" << std::endl;
    std::cout << panels[0][0] << "Demonstrate fixed and flexible panels"
              << std::endl;

    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<int> distribution(0, 9999);

    for (std::size_t line_count = 0;
         sample_count == 0 || line_count < sample_count; ++line_count) {
      const int value = distribution(generator);
      std::cout << panels[1] << line_count << ", " << value << std::endl;
      std::cout << panels[2][1] << "Running..." << std::endl;
      std::cout << panels[2][0] << "Lines appended: " << line_count + 1
                << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    panels.disable();
  } catch (const std::exception &error) {
    std::cerr << "three_panels: " << error.what() << std::endl;
    return 1;
  }

  return 0;
}
