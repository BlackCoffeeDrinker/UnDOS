
#pragma once

#include <string.hpp>
#include <string_view.hpp>

namespace kernel {
class CommandLineParser {
  kstd::string _cmd;

  // Extract next token starting at index i
  kstd::string_view next_token(size_t &i) const {
    // Skip whitespace
    while (i < _cmd.size() && (_cmd[i] == ' ' || _cmd[i] == '\t')) {
      i++;
    }

    if (i >= _cmd.size()) {
      return {};
    }

    const auto start = i;

    // Quoted token
    if (_cmd[i] == '"') {
      i++;// skip opening quote
      const auto token_start = i;

      while (i < _cmd.size()) {
        if (_cmd[i] == '\\' && i + 1 < _cmd.size()) {
          i += 2;// skip escaped char
          continue;
        }
        if (_cmd[i] == '"') {
          const auto tok = _cmd.substr(token_start, i - token_start);
          i++;// skip closing quote
          return tok;
        }
        i++;
      }

      // Unterminated quote → return rest
      return _cmd.substr(token_start);
    }

    // Unquoted token
    while (i < _cmd.size() && _cmd[i] != ' ' && _cmd[i] != '\t') {
      i++;
    }

    return _cmd.substr(start, i - start);
  }

  // Split key=value
  static bool split_key_value(const kstd::string_view &tok,
                              kstd::string_view &key,
                              kstd::string_view &value) {
    const auto eq = tok.find('=');
    if (eq == kstd::string_view::npos) {
      return false;
    }

    key = tok.substr(0, eq);
    value = tok.substr(eq + 1);
    return true;
  }


  public:
  // Owns a copy of the command line so it outlives the BootInfo pages.
  explicit CommandLineParser(const kstd::string_view &cmd) : _cmd(cmd) {}
  [[nodiscard]] kstd::string_view get_command_line() const { return _cmd; }

  [[nodiscard]] bool has_key(const kstd::string_view &key) const {
    size_t i = 0;
    while (true) {
      auto tok = next_token(i);
      if (tok.empty())
        return false;

      kstd::string_view k, v;
      if (split_key_value(tok, k, v)) {
        if (k == key)
          return true;
      } else {
        if (tok == key)
          return true;
      }
    }
  }

  [[nodiscard]] kstd::string_view get(const kstd::string_view &key,
                                      const kstd::string_view &default_value = "") const {
    size_t i = 0;
    while (true) {
      auto tok = next_token(i);
      if (tok.empty())
        return default_value;

      kstd::string_view k, v;
      if (split_key_value(tok, k, v)) {
        if (k == key)
          return v;
      } else {
        if (tok == key)
          return "on";
      }
    }
  }
};


}
