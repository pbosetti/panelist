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
  assert(!panels.enabled());
  output << panels[1] << "disabled output is dropped\n";
  output << panels[2][0] << "disabled addressed output is dropped"
         << std::endl;
  std::ostream &disabled_stream = output << panels[2][0];
  disabled_stream << "disabled converted output is dropped\n";
  assert(output.str() == "top panel\nmiddle panel\nbottom addressed\n");

  panels.enable(true);
  assert(panels.enabled());
  output << panels[1] << "enabled again\n";

  panels.enable(false);
  assert(!panels.enabled());
  output << panels[1] << "enable false output is dropped\n";
  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\nenabled again\n");

  panels.enable();
  assert(panels.enabled());

  panels.reset();
  panels.add_panel();
  panels.layout();
  output << panels[0] << "reused\n";

  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\nenabled again\nreused\n");
}
