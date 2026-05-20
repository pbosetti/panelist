#include <panelist/panelist.hpp>

#include <cassert>
#include <sstream>

int main() {
  std::ostringstream output;

  Panelist::Panelist panels(output);
  panels.add_panel();
  panels.layout();

  output << panels[0] << "fetch content consumer" << std::endl;

  assert(output.str() == "fetch content consumer\n");
}
