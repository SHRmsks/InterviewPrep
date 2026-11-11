// files.hpp — the file helpers the command-line tool and the MCP server share.
#pragma once

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace std;

inline string readFile(const string& path, bool* ok = nullptr) {
  ifstream in(path, ios::binary);
  if (ok) *ok = in.good();
  stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

inline bool exists(const string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}

inline void makeDirectories(const string& path) {
  for (size_t i = 1; i < path.size(); ++i) {
    if (path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
  }
}

// The folder a progress file sits in. The profile goes beside it.
inline string directoryOf(const string& path) {
  const size_t slash = path.rfind('/');
  return slash == string::npos ? "." : path.substr(0, slash);
}

/*
Swaps in a new version in one step, so a reader sees the old file or the new
one and never half of each. The version it replaces stays beside it as .bak.
*/
inline bool replaceFile(const string& path, const string& text) {
  makeDirectories(path);
  const string temporary = path + ".tmp";
  {
    ofstream out(temporary, ios::binary);
    out << text;
    out.flush();
    if (!out.good()) return false;
  }
  if (exists(path)) rename(path.c_str(), (path + ".bak").c_str());
  return rename(temporary.c_str(), path.c_str()) == 0;
}

// One write, so text from two sessions can't interleave.
inline bool appendText(const string& path, const string& text) {
  makeDirectories(path);
  FILE* file = fopen(path.c_str(), "ab");
  if (!file) return false;
  const bool written = fwrite(text.data(), 1, text.size(), file) == text.size();
  return fclose(file) == 0 && written;
}

inline bool appendLine(const string& path, const string& line) { return appendText(path, line + "\n"); }

inline string nowString() {
  time_t t = time(nullptr);
  tm local{};
  localtime_r(&t, &local);
  char buffer[32];
  strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M", &local);
  return buffer;
}
