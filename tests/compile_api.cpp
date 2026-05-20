#include <panelist/panelist.hpp>

#include <cassert>
#include <sstream>

int main() {
  const auto version = Panelist::Panelist::version();
  assert(!version.empty());
  assert(version.front() == 'v');

  std::ostringstream output;

  Panelist::Panelist panels(output);
  panels.set_separator("=");
  panels.add_panel(2);
  panels.add_panel();
  panels.add_panel(1);
  panels.layout();

  output << panels[0] << "top panel\n";
  output << panels[1] << "middle panel" << std::endl;
  output << panels[2][0] << "bottom addressed" << std::endl;

  assert(output.str() == "top panel\nmiddle panel\nbottom addressed\n");

  panels.disable();
  output << panels[1] << "disabled remains transparent\n";
  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\ndisabled remains "
         "transparent\n");

  panels.reset();
  panels.add_panel();
  panels.layout();
  output << panels[0] << "reused\n";

  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\ndisabled remains "
         "transparent\nreused\n");
}
