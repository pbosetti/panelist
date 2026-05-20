#include <panelist/panelist.hpp>

#include <cassert>
#include <sstream>

int main() {
  const auto version = Panelist::Panelist::version();
  assert(!version.empty());
  assert(version.front() == 'v');

  std::ostringstream output;

  Panelist::Panelist panels(output);
  panels.add_panel();
  panels.layout();

  output << panels[0] << "fetch content consumer" << std::endl;

  assert(output.str() == "fetch content consumer\n");
}
