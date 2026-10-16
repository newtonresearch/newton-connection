/*
 File: FakeProcess.h

 ncx-fake-newton (FakeNewton.h) as a process of its own, for the tests that
 dock with it: the object system belongs to one thread, the desktop's dock
 thread, so the fake Newton can't run on a thread of the test.
 */

#ifndef NCX_TESTS_FAKEPROCESS_H
#define NCX_TESTS_FAKEPROCESS_H

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace ncx {

struct FakeProcess
{
  std::thread thread;
  int exitCode = -1;
  std::filesystem::path report;
  // what it reported
  std::vector<std::string> received;      // the desktop's commands, in order
  std::vector<std::string> extensions;    // the extensions it was sent
  size_t packageSize = 0;
  size_t largestBlock = 0;                // the largest block it sent (NCss)
  size_t blankBlocks = 0;                 // the white ones it didn't send
  size_t memoryRead = 0;                  // the bytes of memory it was asked for (NDmp)
  std::string why;

  /** Start inProgram (ncx-fake-newton) on a thread; it keeps trying to
      connect to inPort. */
  void start(const std::string & inProgram, int inPort, const std::string & inOptions)
  {
    report = std::filesystem::temp_directory_path() / ("ncx-fake-" + std::to_string(inPort) + ".txt");
    std::filesystem::remove(report);
    std::string command = "\"" + inProgram + "\" --port " + std::to_string(inPort) + " " + inOptions + " --report \""
                        + report.u8string() + "\"";
#if defined(_WIN32)
    command = "\"" + command + "\"";     // cmd /c takes the outer quotes away
#endif
    thread = std::thread([this, command] {
      int status = std::system(command.c_str());
#if defined(_WIN32)
      exitCode = status;
#else
      exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    });
  }

  /** Wait for it to end; read what it reported. */
  void finish()
  {
    thread.join();
    std::ifstream in(report, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      if (line.compare(0, 9, "received ") == 0)
        received.push_back(line.substr(9));
      else if (line.compare(0, 10, "extension ") == 0)
        extensions.push_back(line.substr(10));
      else if (line.compare(0, 8, "package ") == 0)
        packageSize = (size_t)std::stoul(line.substr(8));
      else if (line.compare(0, 6, "block ") == 0)
        largestBlock = (size_t)std::stoul(line.substr(6));
      else if (line.compare(0, 6, "blank ") == 0)
        blankBlocks = (size_t)std::stoul(line.substr(6));
      else if (line.compare(0, 7, "memory ") == 0)
        memoryRead = (size_t)std::stoul(line.substr(7));
      else if (line.compare(0, 4, "why ") == 0)
        why = line.substr(4);
    }
    in.close();
    std::filesystem::remove(report);
  }
};

} // namespace ncx

#endif // NCX_TESTS_FAKEPROCESS_H
