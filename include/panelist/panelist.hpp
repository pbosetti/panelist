#ifndef PANELIST_PANELIST_HPP
#define PANELIST_PANELIST_HPP

/**
 * @file panelist.hpp
 * @brief Header-only C++17 terminal panel output helper.
 *
 * Panelist divides an interactive terminal into vertically stacked panels and
 * lets callers write either scrolling log output or addressed panel lines using
 * ordinary `std::ostream` insertion syntax. When the wrapped stream is not
 * connected to a supported terminal, panel selections are transparent no-ops
 * and output is written normally. After disable(), panel selections are also
 * transparent no-ops until panel output is enabled again.
 */

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "panelist_version.hpp"

/**
 * @brief Public namespace for the Panelist terminal panel library.
 */
namespace Panelist {

/**
 * @brief Manages a panelized view over a single output stream.
 *
 * A Panelist object owns the layout metadata and temporary stream buffer used
 * to route output to panels. Panels are added before calling layout(); after
 * layout(), `operator[]` returns stream manipulators that select a panel.
 *
 * @code
 * Panelist::Panelist panels(std::cout);
 * panels.add_panel(2);
 * panels.add_panel();
 * panels.add_panel(2);
 * panels.layout();
 *
 * std::cout << panels[1] << "scrolling log line" << std::endl;
 * std::cout << panels[2][0] << "bottom status line" << std::endl;
 * @endcode
 *
 * @note The object is bound to the stream passed to the constructor. In
 * interactive mode, inserting a panel selection into another stream is an
 * error.
 */
class Panelist {
public:
  class LineSelection;

  /**
   * @brief Stream selection proxy for append-style panel output.
   *
   * This proxy is returned by Panelist::operator[] and is meant to be inserted
   * into the same stream managed by the owning Panelist object.
   */
  class PanelSelection {
  public:
    /**
     * @brief Select an addressed line inside the panel.
     *
     * @param line_from_bottom Line index inside the selected panel, where `0`
     * is the bottom line, `1` is the line above it, and so on.
     * @return A LineSelection proxy suitable for stream insertion.
     */
    LineSelection operator[](std::size_t line_from_bottom) const;

  private:
    friend class Panelist;
    friend std::ostream &operator<<(std::ostream &stream,
                                    const PanelSelection &selection);

    PanelSelection(Panelist *owner, std::size_t panel_index)
        : _owner(owner), _panel_index(panel_index) {}

    Panelist *_owner = nullptr;
    std::size_t _panel_index = 0;
  };

  /**
   * @brief Stream selection proxy for addressed-line panel output.
   *
   * Line selections are created with `panels[panel][line]` or line() and are
   * inserted into the managed stream before the text to write.
   */
  class LineSelection {
  private:
    friend class PanelSelection;
    friend class Panelist;
    friend std::ostream &operator<<(std::ostream &stream,
                                    const LineSelection &selection);

    LineSelection(Panelist *owner, std::size_t panel_index,
                  std::size_t line_from_bottom)
        : _owner(owner), _panel_index(panel_index),
          _line_from_bottom(line_from_bottom) {}

    Panelist *_owner = nullptr;
    std::size_t _panel_index = 0;
    std::size_t _line_from_bottom = 0;
  };

  /**
   * @brief Return the Panelist version string.
   *
   * The value is supplied by CMake from the latest reachable git tag matching
   * `vMAJOR.MINOR.PATCH`. If the header is used without CMake, or no matching
   * tag is available at configure time, this returns `v0.0.0`.
   *
   * @return Version string in the form `v0.1.2`.
   */
  static constexpr std::string_view version() noexcept {
    return Version;
  }

  /**
   * @brief Construct a panel manager for an output stream.
   *
   * @param stream Stream that will receive panelized output. `std::cout`,
   * `std::cerr`, and `std::clog` are recognized for terminal detection. Other
   * streams are treated as non-interactive pass-through streams.
   */
  explicit Panelist(std::ostream &stream)
      : _stream(stream), _original_rdbuf(stream.rdbuf()),
        _capture_buffer(*this) {
    _fd = fd_for_stream(stream);
    _interactive = _fd.has_value() && fd_is_terminal(*_fd);
#if defined(_WIN32)
    if (_interactive) {
      _console_handle = handle_for_fd(*_fd);
    }
#endif
  }

  /** @brief Panelist objects cannot be copied. */
  Panelist(const Panelist &) = delete;

  /** @brief Panelist objects cannot be copy-assigned. */
  Panelist &operator=(const Panelist &) = delete;

  /** @brief Panelist objects cannot be moved. */
  Panelist(Panelist &&) = delete;

  /** @brief Panelist objects cannot be move-assigned. */
  Panelist &operator=(Panelist &&) = delete;

  /**
   * @brief Restore the stream and terminal mode before destruction.
   */
  ~Panelist() {
    try {
      reset();
    } catch (...) {
    }
  }

  /**
   * @brief Set the separator text repeated between panels.
   *
   * @param separator Text repeated to fill each separator row. Empty strings
   * are treated as a single space.
   */
  void set_separator(std::string separator) {
    _separator = separator.empty() ? std::string(" ") : std::move(separator);
    if (panel_mode_active()) {
      render_all();
    }
  }

  /**
   * @brief Make the flexible panel scrollable with a history buffer.
   *
   * When called before layout(), the flexible panel will maintain a scroll
   * buffer. The effective buffer length is the maximum of the flexible panel's
   * rendered height and @p buffer_length. The user can scroll the panel with
   * arrow keys (Up/Down), Page Up/Down, and the mouse scroll wheel.
   *
   * If this method is not called, the flexible panel behaves exactly as before
   * (a fixed-size display window with no history).
   *
   * @param buffer_length Minimum number of lines to keep in the scroll buffer.
   *
   * @throws std::logic_error If called after layout().
   */
  void set_scrollable(std::size_t buffer_length) {
    ensure_not_laid_out("set_scrollable");
    _requested_scroll_buffer_length = buffer_length;
  }

  /**
   * @brief Add the flexible panel.
   *
   * The flexible panel receives all terminal rows not consumed by fixed panels
   * and separator rows. Only one flexible panel may be explicitly added.
   *
   * @throws std::logic_error If called after layout() or if a flexible panel
   * was already added.
   */
  void add_panel() {
    ensure_not_laid_out("add_panel");
    if (_explicit_flexible_panel.has_value()) {
      throw std::logic_error("Panelist already has a flexible panel");
    }

    _explicit_flexible_panel = _panel_specs.size();
    _panel_specs.push_back(PanelSpec{std::nullopt});
  }

  /**
   * @brief Add a fixed-height panel.
   *
   * If no explicit flexible panel is added before layout(), the last panel is
   * used as the flexible panel and this height becomes its minimum height.
   *
   * @param height Requested fixed height, in terminal rows.
   *
   * @throws std::logic_error If called after layout().
   * @throws std::invalid_argument If @p height is zero.
   */
  void add_panel(std::size_t height) {
    ensure_not_laid_out("add_panel");
    if (height == 0) {
      throw std::invalid_argument("Panelist panel height must be at least 1");
    }

    _panel_specs.push_back(PanelSpec{height});
  }

  /**
   * @brief Choose which already-added panel is flexible.
   *
   * @param panel_index Zero-based panel index.
   *
   * @throws std::logic_error If called after layout().
   * @throws std::out_of_range If @p panel_index does not name an added panel.
   */
  void set_flexible_panel(std::size_t panel_index) {
    ensure_not_laid_out("set_flexible_panel");
    if (panel_index >= _panel_specs.size()) {
      throw std::out_of_range("Panelist panel index is out of range");
    }

    _explicit_flexible_panel = panel_index;
  }

  /**
   * @brief Finalize the panel list and enable panel mode.
   *
   * This computes the current terminal layout, switches supported terminals to
   * panel mode, and makes subsequent panel selections active. Adding more
   * panels after this call is an error.
   *
   * @throws std::logic_error If no panels were added.
   * @throws std::runtime_error If the interactive terminal is too short for the
   * requested panel layout.
   */
  void layout() {
    if (_panel_specs.empty()) {
      throw std::logic_error("Panelist layout requires at least one panel");
    }

    _effective_flexible_panel =
        _explicit_flexible_panel.value_or(_panel_specs.size() - 1);
    _panel_states.resize(_panel_specs.size());
    _laid_out = true;
    enable();
  }

  /**
   * @brief Re-enable panel mode after disable().
   *
   * @throws std::logic_error If layout() has not been called.
   * @throws std::runtime_error If the interactive terminal is too short for the
   * requested panel layout.
   */
  void enable() { enable(true); }

  /**
   * @brief Enable or disable panel output.
   *
   * Passing `true` is equivalent to enable(); passing `false` is equivalent to
   * disable(). While disabled, panel selections are transparent no-ops and
   * output is written normally.
   *
   * @throws std::logic_error If @p onoff is true and layout() has not been
   * called.
   * @throws std::runtime_error If @p onoff is true and the interactive terminal
   * is too short for the requested panel layout.
   */
  void enable(bool onoff) {
    if (!onoff) {
      if (!_enabled) {
        return;
      }

      if (_interactive) {
        finalize_for_disable();
        render_all();
        restore_stream_buffer();
        std::string exit_seq;
        if (_requested_scroll_buffer_length.has_value()) {
          exit_seq += "\x1b[?1006l\x1b[?1000l"; // disable SGR mouse + mouse tracking
        }
        exit_seq += "\x1b[?25h\x1b[?1049l"; // show cursor, leave alternate screen
        write_raw(exit_seq);
        flush_raw();
        restore_terminal_mode();
      }

      _enabled = false;
      return;
    }

    if (!_laid_out) {
      throw std::logic_error("Panelist::layout must be called before enable");
    }

    if (_enabled) {
      return;
    }

    _enabled = true;
    if (!panel_mode_active()) {
      return;
    }

    enable_terminal_mode();
    recompute_layout(true);
    std::string enter_seq = "\x1b[?1049h\x1b[2J\x1b[?25l"; // alternate screen, clear, hide cursor
    if (_requested_scroll_buffer_length.has_value()) {
      enter_seq += "\x1b[?1000h\x1b[?1006h"; // enable mouse tracking + SGR extended coords
    }
    write_raw(enter_seq);
    render_all();
  }

  /**
   * @brief Temporarily leave panel mode and restore normal stream output.
   *
   * Existing panel definitions and buffered panel contents are preserved, so
   * enable() can later re-enter panel mode. While disabled, panel selections
   * are transparent no-ops and output is written normally.
   */
  void disable() { enable(false); }

  /**
   * @brief Remove the current layout and return to the pre-layout state.
   *
   * After reset(), panels may be added again and layout() may be called to
   * create a new arrangement.
   */
  void reset() {
    disable();
    _panel_specs.clear();
    _panel_states.clear();
    _panel_geometry.clear();
    _explicit_flexible_panel.reset();
    _effective_flexible_panel.reset();
    _terminal_size.reset();
    _active_target.reset();
    _requested_scroll_buffer_length.reset();
    _laid_out = false;
  }

  /**
   * @brief Alias for reset().
   */
  void remove() { reset(); }

  /**
   * @brief Alias for reset().
   *
   * This avoids using `delete`, which is a C++ keyword and cannot be a member
   * function name.
   */
  void delete_panels() { reset(); }

  /**
   * @brief Clear all visible and pending contents from one panel.
   *
   * @param panel_index Zero-based panel index.
   *
   * @throws std::out_of_range If @p panel_index does not name an added panel.
   */
  void clear(std::size_t panel_index) {
    ensure_panel_index(panel_index);

    if (panel_index < _panel_states.size()) {
      auto &state = _panel_states[panel_index];
      if (is_flexible_scrollable_panel(panel_index)) {
        state.lines.clear();
        state.scroll_offset = 0;
      } else {
        std::fill(state.lines.begin(), state.lines.end(), std::string());
      }
      state.log_pending.clear();
    }

    if (_active_target.has_value() &&
        _active_target->panel_index == panel_index) {
      _active_target.reset();
    }

    if (panel_mode_active()) {
      render_all();
    }
  }

  /**
   * @brief Select a panel for append-style scrolling output.
   *
   * @param panel_index Zero-based panel index.
   * @return A PanelSelection proxy for stream insertion.
   *
   * @code
   * std::cout << panels[1] << "log line" << std::endl;
   * @endcode
   */
  PanelSelection operator[](std::size_t panel_index) {
    return PanelSelection(this, panel_index);
  }

  /**
   * @brief Select an addressed line inside a panel.
   *
   * This is equivalent to `panels[panel_index][line_from_bottom]` and is useful
   * when a named function call is clearer.
   *
   * @param panel_index Zero-based panel index.
   * @param line_from_bottom Line index inside the panel, where `0` is the
   * bottom line.
   * @return A LineSelection proxy for stream insertion.
   */
  LineSelection line(std::size_t panel_index, std::size_t line_from_bottom) {
    return LineSelection(this, panel_index, line_from_bottom);
  }

  /**
   * @brief Return the number of configured panels.
   */
  std::size_t panel_count() const { return _panel_specs.size(); }

  /**
   * @brief Return whether panel mode is currently enabled.
   */
  bool enabled() const { return _enabled; }

  /**
   * @brief Return whether the managed stream is attached to a supported
   * interactive terminal.
   */
  bool interactive() const { return _interactive; }

private:
  struct TerminalSize {
    std::size_t rows = 0;
    std::size_t cols = 0;

    bool operator==(const TerminalSize &other) const {
      return rows == other.rows && cols == other.cols;
    }
  };

  struct PanelSpec {
    std::optional<std::size_t> requested_height;
  };

  struct PanelState {
    std::vector<std::string> lines;
    std::string log_pending;
    std::size_t scroll_offset = 0;   // lines scrolled back from bottom (0 = following tail)
    std::size_t buffer_capacity = 0; // effective history buffer size (0 = non-scrollable)
  };

  struct PanelGeometry {
    std::size_t start_row = 0;
    std::size_t height = 0;
  };

  struct Target {
    std::size_t panel_index = 0;
    std::optional<std::size_t> line_from_bottom;
    std::size_t line_cursor = 0;
    std::string pending;
  };

  class CaptureBuffer : public std::streambuf {
  public:
    explicit CaptureBuffer(Panelist &owner) : _owner(owner) {}

  protected:
    int_type overflow(int_type value) override {
      if (traits_type::eq_int_type(value, traits_type::eof())) {
        return traits_type::not_eof(value);
      }

      const char ch = traits_type::to_char_type(value);
      _owner.write_to_active_target(std::string_view(&ch, 1));
      return value;
    }

    std::streamsize xsputn(const char *data,
                           std::streamsize count) override {
      if (count <= 0) {
        return 0;
      }

      _owner.write_to_active_target(
          std::string_view(data, static_cast<std::size_t>(count)));
      return count;
    }

    int sync() override {
      _owner.sync_capture_buffer();
      return 0;
    }

  private:
    Panelist &_owner;
  };

  friend std::ostream &operator<<(std::ostream &stream,
                                  const PanelSelection &selection);
  friend std::ostream &operator<<(std::ostream &stream,
                                  const LineSelection &selection);

  static std::optional<int> fd_for_stream(const std::ostream &stream) {
    if (&stream == &std::cout) {
#if defined(_WIN32)
      return _fileno(stdout);
#else
      return fileno(stdout);
#endif
    }

    if (&stream == &std::cerr || &stream == &std::clog) {
#if defined(_WIN32)
      return _fileno(stderr);
#else
      return fileno(stderr);
#endif
    }

    return std::nullopt;
  }

  static bool fd_is_terminal(int fd) {
#if defined(_WIN32)
    return _isatty(fd) != 0;
#else
    return isatty(fd) != 0;
#endif
  }

  static std::size_t positive_env_value(const char *name,
                                        std::size_t fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
      return fallback;
    }

    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed == 0) {
      return fallback;
    }

    return static_cast<std::size_t>(parsed);
  }

#if defined(_WIN32)
  static HANDLE handle_for_fd(int fd) {
    if (fd == _fileno(stderr)) {
      return GetStdHandle(STD_ERROR_HANDLE);
    }

    return GetStdHandle(STD_OUTPUT_HANDLE);
  }
#endif

  bool panel_mode_active() const {
    return _interactive && _laid_out && _enabled;
  }

  bool is_flexible_scrollable_panel(std::size_t panel_index) const {
    return _effective_flexible_panel.has_value() &&
           *_effective_flexible_panel == panel_index &&
           _requested_scroll_buffer_length.has_value();
  }

  void ensure_not_laid_out(std::string_view function_name) const {
    if (_laid_out) {
      throw std::logic_error(std::string("Panelist::") +
                             std::string(function_name) +
                             " cannot be called after layout");
    }
  }

  void ensure_panel_index(std::size_t panel_index) const {
    if (panel_index >= _panel_specs.size()) {
      throw std::out_of_range("Panelist panel index is out of range");
    }
  }

  void ensure_layout_current() {
    if (!panel_mode_active()) {
      return;
    }

    const TerminalSize size = read_terminal_size();
    if (!_terminal_size.has_value() || !(*_terminal_size == size)) {
      recompute_layout(true);
      write_raw("\x1b[2J");
    }
  }

  TerminalSize read_terminal_size() const {
    TerminalSize size{positive_env_value("LINES", 24),
                      positive_env_value("COLUMNS", 80)};

    if (!_fd.has_value()) {
      return size;
    }

#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (_console_handle != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(_console_handle, &info)) {
      const SHORT rows = info.srWindow.Bottom - info.srWindow.Top + 1;
      const SHORT cols = info.srWindow.Right - info.srWindow.Left + 1;
      if (rows > 0 && cols > 0) {
        size.rows = static_cast<std::size_t>(rows);
        size.cols = static_cast<std::size_t>(cols);
      }
    }
#else
    struct winsize window_size {};
    if (ioctl(*_fd, TIOCGWINSZ, &window_size) == 0) {
      if (window_size.ws_row > 0) {
        size.rows = static_cast<std::size_t>(window_size.ws_row);
      }
      if (window_size.ws_col > 0) {
        size.cols = static_cast<std::size_t>(window_size.ws_col);
      }
    }
#endif

    return size;
  }

  void recompute_layout(bool force) {
    if (!panel_mode_active()) {
      return;
    }

    const TerminalSize size = read_terminal_size();
    if (!force && _terminal_size.has_value() && *_terminal_size == size) {
      return;
    }

    if (!_effective_flexible_panel.has_value()) {
      throw std::logic_error("Panelist has no flexible panel");
    }

    const std::size_t panel_count = _panel_specs.size();
    const std::size_t separator_count = panel_count > 0 ? panel_count - 1 : 0;
    if (size.rows <= separator_count) {
      throw std::runtime_error("terminal is too short for Panelist layout");
    }

    const std::size_t flexible_panel = *_effective_flexible_panel;
    std::size_t fixed_height = 0;
    std::size_t flexible_min_height = 1;

    for (std::size_t index = 0; index < panel_count; ++index) {
      const auto requested_height = _panel_specs[index].requested_height;
      if (index == flexible_panel) {
        flexible_min_height = requested_height.value_or(1);
      } else {
        fixed_height += requested_height.value_or(1);
      }
    }

    const std::size_t available_panel_height = size.rows - separator_count;
    if (available_panel_height < fixed_height + flexible_min_height) {
      throw std::runtime_error("terminal is too short for Panelist panels");
    }

    std::vector<std::size_t> heights(panel_count, 1);
    for (std::size_t index = 0; index < panel_count; ++index) {
      if (index == flexible_panel) {
        heights[index] = available_panel_height - fixed_height;
      } else {
        heights[index] = _panel_specs[index].requested_height.value_or(1);
      }
    }

    _panel_states.resize(panel_count);
    _panel_geometry.resize(panel_count);

    std::size_t row = 1;
    for (std::size_t index = 0; index < panel_count; ++index) {
      resize_panel_state(index, heights[index]);
      _panel_geometry[index] = PanelGeometry{row, heights[index]};
      row += heights[index] + (index + 1 < panel_count ? 1 : 0);
    }

    _terminal_size = size;
  }

  void resize_panel_state(std::size_t panel_index, std::size_t height) {
    auto &state = _panel_states[panel_index];
    auto &lines = state.lines;

    if (is_flexible_scrollable_panel(panel_index)) {
      // For a scrollable panel, `lines` is the history buffer; its size is
      // max(height, requested_buffer_length) and grows up to that capacity.
      const std::size_t new_cap =
          (std::max)(height, *_requested_scroll_buffer_length);
      state.buffer_capacity = new_cap;

      // Trim history buffer to new capacity, adjusting scroll offset so the
      // viewed window does not jump.
      while (lines.size() > new_cap) {
        lines.erase(lines.begin());
        if (state.scroll_offset > 0) {
          --state.scroll_offset;
        }
      }

      // Clamp scroll offset to the valid range for the new height.
      const std::size_t max_off =
          lines.size() > height ? lines.size() - height : 0;
      state.scroll_offset = (std::min)(state.scroll_offset, max_off);
      return;
    }

    // Non-scrollable: fixed-size window of exactly `height` lines.
    if (lines.size() == height) {
      return;
    }

    std::vector<std::string> resized(height);
    const std::size_t kept_lines = (std::min)(height, lines.size());
    const std::size_t old_start = lines.size() - kept_lines;
    const std::size_t new_start = height - kept_lines;
    for (std::size_t offset = 0; offset < kept_lines; ++offset) {
      resized[new_start + offset] = std::move(lines[old_start + offset]);
    }

    lines = std::move(resized);
  }

  void enable_terminal_mode() {
#if defined(_WIN32)
    if (_console_handle == INVALID_HANDLE_VALUE) {
      return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(_console_handle, &mode)) {
      return;
    }

    if (!_console_mode_saved) {
      _original_console_mode = mode;
      _console_mode_saved = true;
    }

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(_console_handle, mode);
#else
    if (_requested_scroll_buffer_length.has_value() && !_stdin_termios_saved) {
    if (tcgetattr(STDIN_FILENO, &_original_stdin_termios) == 0) {
      _stdin_termios_saved = true;
      struct termios raw = _original_stdin_termios;
      raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | ISIG);
      raw.c_iflag &= ~static_cast<tcflag_t>(ICRNL | IXON);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    }
#endif
  }

  void restore_terminal_mode() {
#if defined(_WIN32)
    if (_console_mode_saved && _console_handle != INVALID_HANDLE_VALUE) {
    SetConsoleMode(_console_handle, _original_console_mode);
    }
#else
    if (_stdin_termios_saved) {
    tcsetattr(STDIN_FILENO, TCSANOW, &_original_stdin_termios);
    _stdin_termios_saved = false;
    }
#endif
  }

  void install_capture_buffer() {
    if (_capture_installed) {
      return;
    }

    _stream.rdbuf(&_capture_buffer);
    _capture_installed = true;
  }

  void restore_stream_buffer() {
    if (!_capture_installed) {
      return;
    }

    _stream.rdbuf(_original_rdbuf);
    _capture_installed = false;
  }

  void begin_capture(std::ostream &stream, std::size_t panel_index,
                     std::optional<std::size_t> line_from_bottom) {
    if (!panel_mode_active()) {
      return;
    }

    if (&stream != &_stream) {
      throw std::logic_error(
          "Panelist selection was inserted into a different stream");
    }

    ensure_layout_current();
    ensure_panel_index(panel_index);

    if (line_from_bottom.has_value()) {
      const auto &geometry = _panel_geometry[panel_index];
      if (*line_from_bottom >= geometry.height) {
        throw std::out_of_range("Panelist line index is out of range");
      }
    }

    Target next;
    next.panel_index = panel_index;
    next.line_from_bottom = line_from_bottom;
    next.line_cursor = line_from_bottom.value_or(0);

    if (_active_target.has_value() && same_target(*_active_target, next)) {
      if (next.line_from_bottom.has_value()) {
        _active_target = std::move(next);
        if (clear_addressed_target_line(*_active_target)) {
          render_all();
        }
      }

      install_capture_buffer();
      return;
    }

    finalize_for_target_switch(next);
    _active_target = std::move(next);
    if (clear_addressed_target_line(*_active_target)) {
      render_all();
    }
    install_capture_buffer();
  }

  bool clear_addressed_target_line(const Target &target) {
    if (!target.line_from_bottom.has_value()) {
      return false;
    }

    set_addressed_line(target.panel_index, *target.line_from_bottom,
                       std::string());
    return true;
  }

  static bool same_target(const Target &lhs, const Target &rhs) {
    return lhs.panel_index == rhs.panel_index &&
           lhs.line_from_bottom == rhs.line_from_bottom;
  }

  void finalize_for_target_switch(const Target &next) {
    if (!_active_target.has_value()) {
      return;
    }

    if (_active_target->line_from_bottom.has_value()) {
      commit_address_pending(false);
      return;
    }

    if (_active_target->panel_index != next.panel_index ||
        next.line_from_bottom.has_value()) {
      commit_log_pending(_active_target->panel_index);
    }
  }

  void finalize_for_disable() {
    if (_active_target.has_value() &&
        _active_target->line_from_bottom.has_value()) {
      commit_address_pending(false);
    }

    for (std::size_t index = 0; index < _panel_states.size(); ++index) {
      commit_log_pending(index);
    }

    _active_target.reset();
  }

  void write_to_active_target(std::string_view text) {
    if (!panel_mode_active() || !_active_target.has_value()) {
      write_raw(text);
      return;
    }

    ensure_layout_current();

    if (_active_target->line_from_bottom.has_value()) {
      write_to_addressed_line(text);
    } else {
      write_to_log(text);
    }

    render_all();
  }

  void write_to_log(std::string_view text) {
    auto &state = _panel_states[_active_target->panel_index];
    for (char ch : text) {
      if (ch == '\r') {
        state.log_pending.clear();
      } else if (ch == '\n') {
        append_log_line(_active_target->panel_index, state.log_pending);
        state.log_pending.clear();
      } else {
        state.log_pending.push_back(ch);
      }
    }
  }

  void write_to_addressed_line(std::string_view text) {
    for (char ch : text) {
      if (ch == '\r') {
        _active_target->pending.clear();
      } else if (ch == '\n') {
        commit_address_pending(true);
        ++_active_target->line_cursor;
      } else {
        _active_target->pending.push_back(ch);
      }
    }
  }

  void sync_capture_buffer() {
    if (panel_mode_active()) {
      if (_active_target.has_value() &&
          _active_target->line_from_bottom.has_value()) {
        commit_address_pending(false);
      }
      render_all();
    }

    flush_raw();
  }

  void append_log_line(std::size_t panel_index, const std::string &line) {
    auto &state = _panel_states[panel_index];
    auto &lines = state.lines;

    if (is_flexible_scrollable_panel(panel_index)) {
      // Evict oldest line when buffer is at capacity.
      if (state.buffer_capacity > 0 && lines.size() >= state.buffer_capacity) {
        lines.erase(lines.begin());
        // After eviction the scroll_offset needs no adjustment: the new line
        // appended below will increment the offset (see below), net effect
        // maintains the same viewed window.
      }
      lines.push_back(line);

      // If scrolled up, keep the currently viewed window stable by advancing
      // scroll_offset to account for the newly appended line at the tail.
      if (state.scroll_offset > 0 && !_panel_geometry.empty() &&
          panel_index < _panel_geometry.size()) {
        const std::size_t height = _panel_geometry[panel_index].height;
        const std::size_t max_off =
            lines.size() > height ? lines.size() - height : 0;
        state.scroll_offset = (std::min)(state.scroll_offset + 1, max_off);
      }
    } else {
      if (lines.empty()) {
        return;
      }
      lines.erase(lines.begin());
      lines.push_back(line);
    }
  }

  void commit_log_pending(std::size_t panel_index) {
    if (panel_index >= _panel_states.size()) {
      return;
    }

    auto &pending = _panel_states[panel_index].log_pending;
    if (pending.empty()) {
      return;
    }

    append_log_line(panel_index, pending);
    pending.clear();
  }

  void commit_address_pending(bool commit_empty) {
    if (!_active_target.has_value() ||
        !_active_target->line_from_bottom.has_value()) {
      return;
    }

    if (!commit_empty && _active_target->pending.empty()) {
      return;
    }

    set_addressed_line(_active_target->panel_index,
                       _active_target->line_cursor, _active_target->pending);
    _active_target->pending.clear();
  }

  void set_addressed_line(std::size_t panel_index,
                          std::size_t line_from_bottom,
                          const std::string &line) {
    if (panel_index >= _panel_states.size() ||
        panel_index >= _panel_geometry.size()) {
      return;
    }

    const auto &geometry = _panel_geometry[panel_index];
    if (line_from_bottom >= geometry.height) {
      return;
    }

    auto &lines = _panel_states[panel_index].lines;
    const std::size_t line_index = geometry.height - line_from_bottom - 1;
    lines[line_index] = line;
  }

  void render_all() {
    if (!panel_mode_active()) {
      return;
    }

    recompute_layout(false);

    // Poll for keyboard/mouse scroll events before rendering so the new
    // scroll_offset is reflected in this frame.
    if (_requested_scroll_buffer_length.has_value()) {
      poll_scroll_input();
    }

    for (std::size_t panel_index = 0; panel_index < _panel_geometry.size();
         ++panel_index) {
      render_panel(panel_index);
      if (panel_index + 1 < _panel_geometry.size()) {
        render_separator(_panel_geometry[panel_index].start_row +
                         _panel_geometry[panel_index].height);
      }
    }

    flush_raw();
  }

  // ---------------------------------------------------------------------------
  // Scroll input handling (keyboard arrows and mouse wheel)
  // ---------------------------------------------------------------------------

  void apply_scroll(int delta) {
    if (!_effective_flexible_panel.has_value()) {
      return;
    }
    const std::size_t flex = *_effective_flexible_panel;
    if (flex >= _panel_states.size() || flex >= _panel_geometry.size()) {
      return;
    }
    auto &state = _panel_states[flex];
    const std::size_t height = _panel_geometry[flex].height;
    const std::size_t max_off =
        state.lines.size() > height ? state.lines.size() - height : 0;

    if (delta > 0) {
      // Scroll up (toward history).
      const std::size_t step = static_cast<std::size_t>(delta);
      state.scroll_offset =
          (std::min)(state.scroll_offset + step, max_off);
    } else if (delta < 0) {
      // Scroll down (toward tail).
      const std::size_t step = static_cast<std::size_t>(-delta);
      state.scroll_offset =
          step >= state.scroll_offset ? 0 : state.scroll_offset - step;
    }
  }

  // Process a chunk of raw bytes from the terminal input and update scroll.
  void process_scroll_input(const char *data, std::size_t len) {
    if (!_effective_flexible_panel.has_value() ||
        _panel_geometry.empty()) {
      return;
    }
    const std::size_t height =
        _panel_geometry[*_effective_flexible_panel].height;
    bool changed = false;

    std::size_t i = 0;
    while (i < len) {
      const unsigned char ch = static_cast<unsigned char>(data[i]);
      if (ch == 0x1b && i + 1 < len && data[i + 1] == '[') {
        if (i + 2 < len) {
          const char third = data[i + 2];
          if (third == 'A') {
            // Up arrow: scroll up 1 line.
            apply_scroll(1);
            changed = true;
            i += 3;
            continue;
          }
          if (third == 'B') {
            // Down arrow: scroll down 1 line.
            apply_scroll(-1);
            changed = true;
            i += 3;
            continue;
          }
          if (third == '<') {
            // SGR mouse: \x1b[<Btn;X;YM or \x1b[<Btn;X;Ym
            std::size_t j = i + 3;
            while (j < len && data[j] != 'M' && data[j] != 'm') {
              ++j;
            }
            if (j < len) {
              // Parse button number (the part before the first ';').
              std::size_t btn = 0;
              std::size_t k = i + 3;
              while (k < j && data[k] != ';') {
                if (data[k] >= '0' && data[k] <= '9') {
                  btn = btn * 10 + static_cast<std::size_t>(data[k] - '0');
                }
                ++k;
              }
              if (btn == 64) {
                apply_scroll(1);  // wheel up
                changed = true;
              } else if (btn == 65) {
                apply_scroll(-1); // wheel down
                changed = true;
              }
              i = j + 1;
              continue;
            }
            // Incomplete sequence — skip ESC and retry.
            ++i;
            continue;
          }
          if (third >= '0' && third <= '9') {
            // Possibly \x1b[5~ (Page Up) or \x1b[6~ (Page Down).
            std::size_t j = i + 3;
            while (j < len && data[j] != '~') {
              ++j;
            }
            if (j < len) {
              const int num = third - '0';
              if (num == 5) {
                apply_scroll(static_cast<int>(height)); // Page Up
                changed = true;
              } else if (num == 6) {
                apply_scroll(-static_cast<int>(height)); // Page Down
                changed = true;
              }
              i = j + 1;
              continue;
            }
            ++i;
            continue;
          }
        }
        ++i; // skip unrecognised ESC sequence byte
      } else {
        ++i;
      }
    }
    (void)changed; // rendered in the next render_all call
  }

  void poll_scroll_input() {
#if !defined(_WIN32)
    if (!_stdin_termios_saved) {
      return;
    }
    char buf[128];
    for (;;) {
      const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
      if (n <= 0) {
        break;
      }
      process_scroll_input(buf, static_cast<std::size_t>(n));
    }
#endif
  }


  void render_panel(std::size_t panel_index) {
    const auto &geometry = _panel_geometry[panel_index];
    const auto &state = _panel_states[panel_index];
    const bool scrollable = is_flexible_scrollable_panel(panel_index);

    for (std::size_t line_index = 0; line_index < geometry.height;
         ++line_index) {
      std::string line;

      if (scrollable) {
        // Determine the visible slice of the history buffer.
        // scroll_offset=0: show the tail (newest `height` lines).
        // scroll_offset=k: show lines [buf_size-height-k, buf_size-k).
        const std::size_t buf_size = state.lines.size();
        const std::size_t visible_end =
            buf_size > state.scroll_offset ? buf_size - state.scroll_offset : 0;
        const std::size_t visible_start =
            visible_end > geometry.height ? visible_end - geometry.height : 0;
        const std::size_t buf_idx = visible_start + line_index;
        line = buf_idx < buf_size ? state.lines[buf_idx] : std::string();

        // Show partially-typed log line at the bottom only when at tail.
        if (!state.log_pending.empty() && line_index + 1 == geometry.height &&
            state.scroll_offset == 0) {
          line = state.log_pending;
        }
      } else {
        line = line_index < state.lines.size() ? state.lines[line_index]
                                               : std::string();

        if (!state.log_pending.empty() && line_index + 1 == geometry.height) {
          line = state.log_pending;
        }

        if (_active_target.has_value() &&
            _active_target->panel_index == panel_index &&
            _active_target->line_from_bottom.has_value() &&
            !_active_target->pending.empty() &&
            _active_target->line_cursor < geometry.height) {
          const std::size_t active_line =
              geometry.height - _active_target->line_cursor - 1;
          if (line_index == active_line) {
            line = _active_target->pending;
          }
        }
      }

      write_cursor_position(geometry.start_row + line_index, 1);
      write_raw("\x1b[2K");
      write_clipped(line);
    }
  }

  void render_separator(std::size_t row) {
    write_cursor_position(row, 1);
    write_raw("\x1b[2K");
    if (!_terminal_size.has_value() || _terminal_size->cols == 0) {
      return;
    }

    const std::size_t cols = _terminal_size->cols;
    std::string line;
    line.reserve(cols);
    while (line.size() < cols) {
      line += _separator;
    }
    if (line.size() > cols) {
      line.resize(cols);
    }

    write_raw(line);
  }

  void write_cursor_position(std::size_t row, std::size_t col) {
    write_raw("\x1b[");
    write_raw(std::to_string(row));
    write_raw(";");
    write_raw(std::to_string(col));
    write_raw("H");
  }

  void write_clipped(const std::string &line) {
    if (!_terminal_size.has_value() || _terminal_size->cols == 0) {
      return;
    }

    const std::size_t count = (std::min)(line.size(), _terminal_size->cols);
    if (count > 0) {
      write_raw(std::string_view(line.data(), count));
    }
  }

  void write_raw(std::string_view text) {
    if (_original_rdbuf == nullptr || text.empty()) {
      return;
    }

    _original_rdbuf->sputn(text.data(),
                           static_cast<std::streamsize>(text.size()));
  }

  void flush_raw() {
    if (_original_rdbuf != nullptr) {
      _original_rdbuf->pubsync();
    }
  }

  std::ostream &_stream;
  std::streambuf *_original_rdbuf = nullptr;
  CaptureBuffer _capture_buffer;
  bool _capture_installed = false;

  std::optional<int> _fd;
  bool _interactive = false;
  bool _laid_out = false;
  bool _enabled = false;

  std::vector<PanelSpec> _panel_specs;
  std::vector<PanelState> _panel_states;
  std::vector<PanelGeometry> _panel_geometry;
  std::optional<std::size_t> _explicit_flexible_panel;
  std::optional<std::size_t> _effective_flexible_panel;
  std::optional<TerminalSize> _terminal_size;
  std::optional<Target> _active_target;
  std::string _separator = "-";
  std::optional<std::size_t> _requested_scroll_buffer_length;

#if defined(_WIN32)
  HANDLE _console_handle = INVALID_HANDLE_VALUE;
  DWORD _original_console_mode = 0;
  bool _console_mode_saved = false;
#else
  struct termios _original_stdin_termios {};
  bool _stdin_termios_saved = false;
#endif
};

inline Panelist::LineSelection
Panelist::PanelSelection::operator[](std::size_t line_from_bottom) const {
  return LineSelection(_owner, _panel_index, line_from_bottom);
}

/**
 * @brief Select a panel as the target for subsequent appended output.
 *
 * @param stream Stream that must be the stream managed by the owning Panelist
 * object when panel mode is active.
 * @param selection Panel selection proxy returned by Panelist::operator[].
 * @return @p stream.
 *
 * @throws std::logic_error If @p selection is inserted into a different stream
 * while panel mode is active.
 * @throws std::out_of_range If the selected panel index is invalid while panel
 * mode is active.
 */
inline std::ostream &operator<<(std::ostream &stream,
                                const Panelist::PanelSelection &selection) {
  if (selection._owner != nullptr) {
    selection._owner->begin_capture(stream, selection._panel_index,
                                    std::nullopt);
  }

  return stream;
}

/**
 * @brief Select a panel line as the target for subsequent addressed output.
 *
 * @param stream Stream that must be the stream managed by the owning Panelist
 * object when panel mode is active.
 * @param selection Line selection proxy returned by
 * Panelist::PanelSelection::operator[] or Panelist::line().
 * @return @p stream.
 *
 * @throws std::logic_error If @p selection is inserted into a different stream
 * while panel mode is active.
 * @throws std::out_of_range If the selected panel or line index is invalid
 * while panel mode is active.
 */
inline std::ostream &operator<<(std::ostream &stream,
                                const Panelist::LineSelection &selection) {
  if (selection._owner != nullptr) {
    selection._owner->begin_capture(stream, selection._panel_index,
                                    selection._line_from_bottom);
  }

  return stream;
}

} // namespace Panelist

#endif // PANELIST_PANELIST_HPP
