/*
 File: fake_newton_main.cc

 ncx-fake-newton: the test Newton (FakeNewton.h) as a program, for testing
 ncx-cli, the app, and the comms layer (comms_test.cc runs it: the object
 system is the dock thread's alone, so the fake Newton runs in its own
 process).

   ncx-fake-newton [--port <port>] [--plain] [--password <text>] [--no-toolkit] [--browse-install]
                   [--round <n>] [--report <file>]

 --plain: as a Newton with the Internet Enabler (plain TCP), not Einstein's
 serial port (MNP). --no-toolkit: without the Toolkit app (no
 screenshots). --browse-install: browse the desktop and install a
 package, as Dock's Install does, then disconnect. --round 2: its soups at
 a later backup (FakeNewton.h). --report: what the desktop did, into a file: a line
 "received <command>" per event, "package <bytes>", "why <text>". Exit 0
 if it docked and the desktop disconnected.
 */

#include "FakeNewton.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char * argv[])
{
  ncx::FakeNewtonOptions options;
  std::string report;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
      options.port = std::atoi(argv[++i]);
    else if (arg == "--plain")
      options.plainTCP = true;
    else if (arg == "--password" && i + 1 < argc)
      options.password = argv[++i];
    else if (arg == "--no-toolkit")
      options.toolkit = false;
    else if (arg == "--browse-install")
      options.browseInstall = true;
    else if (arg == "--round" && i + 1 < argc)
      options.round = std::atoi(argv[++i]);
    else if (arg == "--report" && i + 1 < argc)
      report = argv[++i];
    else {
      std::fprintf(stderr, "Usage: ncx-fake-newton [--port <port>] [--plain] [--password <text>] [--no-toolkit] [--browse-install] [--round <n>] [--report <file>]\n");
      return 2;
    }
  }
  ncx::FakeNewton newton;
  bool ok = newton.run(options);
  std::fprintf(stderr, "ncx-fake-newton: %s\n", ok ? "docked, and the desktop disconnected" : newton.why().c_str());
  if (!report.empty()) {
    std::ofstream out(report, std::ios::binary);
    for (const std::string & command : newton.received())
      out << "received " << command << "\n";
    out << "package " << newton.packageSize() << "\n";
    for (const std::string & id : newton.extensions())
      out << "extension " << id << "\n";
    out << "block " << newton.largestBlock() << "\n";
    out << "blank " << newton.blankBlocks() << "\n";
    out << "memory " << newton.memoryRead() << "\n";
    out << "packages " << newton.packagesLoaded() << "\n";
    for (const std::string & name : newton.removed())
      out << "removed " << name << "\n";
    for (const std::string & line : newton.browsed())
      out << "browsed " << line << "\n";
    for (const std::string & soup : newton.soupInfos())
      out << "soupinfo " << soup << "\n";
    for (int64_t id : newton.deleted())
      out << "deleted " << id << "\n";
    for (const std::string & title : newton.added())
      out << "added " << title << "\n";
    for (const std::string & line : newton.restored())
      out << "restored " << line << "\n";
    out << "why " << newton.why() << "\n";
  }
  return ok ? 0 : 1;
}
