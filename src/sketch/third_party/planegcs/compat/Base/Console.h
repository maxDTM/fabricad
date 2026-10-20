#pragma once
#include <format>
#include <iostream>
namespace Base {
struct ConsoleShim {
  template<class... A> void log(std::format_string<A...> f, A&&... a){ std::clog << std::format(f, std::forward<A>(a)...); }
  template<class... A> void warning(std::format_string<A...> f, A&&... a){ std::cerr << std::format(f, std::forward<A>(a)...); }
  template<class... A> void error(std::format_string<A...> f, A&&... a){ std::cerr << std::format(f, std::forward<A>(a)...); }
};
inline ConsoleShim& Console(){ static ConsoleShim c; return c; }
}
