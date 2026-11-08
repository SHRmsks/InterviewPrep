// files.hpp — the file helpers the command-line tool and the MCP server share.
#pragma once

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

inline std::string readFile(const std::string& path, bool* ok = nullptr) {
  std::ifstream in(path, std::ios::binary);
  if (ok) *ok = in.good();
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

inline bool exists(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}

inline void makeDirectories(const std::string& path) {
  for (size_t i = 1; i < path.size(); ++i) {
    if (path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
  }
}

// The folder a progress file sits in. The profile goes beside it.
inline std::string directoryOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? "." : path.substr(0, slash);
}

/*
Swaps in a new version in one step, so a reader sees the old file or the new
one and never half of each. The version it replaces stays beside it as .bak.
*/
inline bool replaceFile(const std::string& path, const std::string& text) {
  makeDirectories(path);
  const std::string temporary = path + ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary);
    out << text;
    out.flush();
    if (!out.good()) return false;
  }
  if (exists(path)) std::rename(path.c_str(), (path + ".bak").c_str());
  return std::rename(temporary.c_str(), path.c_str()) == 0;
}

// One write, so text from two sessions can't interleave.
inline bool appendText(const std::string& path, const std::string& text) {
  makeDirectories(path);
  std::FILE* file = std::fopen(path.c_str(), "ab");
  if (!file) return false;
  const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  return std::fclose(file) == 0 && written;
}

inline bool appendLine(const std::string& path, const std::string& line) { return appendText(path, line + "\n"); }

inline std::string nowString() {
  std::time_t t = std::time(nullptr);
  std::tm local{};
  localtime_r(&t, &local);
  char buffer[32];
  std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M", &local);
  return buffer;
}
