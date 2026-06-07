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
  output << panels[1] << "disabled remains transparent\n";
  output << panels[2][0] << "disabled addressed remains transparent"
         << std::endl;
  std::ostream &disabled_stream = output << panels[2][0];
  disabled_stream << "disabled converted remains transparent\n";
  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\ndisabled remains "
         "transparent\ndisabled addressed remains transparent\n"
         "disabled converted remains transparent\n");

  panels.enable(true);
  assert(panels.enabled());
  output << panels[1] << "enabled again\n";

  panels.enable(false);
  assert(!panels.enabled());
  output << panels[1] << "enable false remains transparent\n";
  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\ndisabled remains "
         "transparent\ndisabled addressed remains transparent\n"
         "disabled converted remains transparent\nenabled again\n"
         "enable false remains transparent\n");

  panels.enable();
  assert(panels.enabled());

  panels.reset();
  panels.add_panel();
  panels.layout();
  output << panels[0] << "reused\n";

  assert(output.str() ==
         "top panel\nmiddle panel\nbottom addressed\ndisabled remains "
         "transparent\ndisabled addressed remains transparent\n"
         "disabled converted remains transparent\nenabled again\n"
         "enable false remains transparent\nreused\n");

  // Verify that set_scrollable compiles and is accepted before layout().
  {
    std::ostringstream scroll_out;
    Panelist::Panelist scroll_panels(scroll_out);
    scroll_panels.add_panel(2);
    scroll_panels.add_panel(); // flexible panel
    scroll_panels.add_panel(1);
    scroll_panels.set_scrollable(200); // request a 200-line history buffer
    scroll_panels.layout();

    // In non-interactive mode the scroll buffer is still populated; lines go
    // straight through to the stream as normal.
    for (int i = 0; i < 10; ++i) {
      scroll_out << scroll_panels[1] << "line " << i << "\n";
    }
    scroll_panels.disable();
  }
}
