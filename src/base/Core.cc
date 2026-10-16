/*
 File: Core.cc

 Starting the core, and its self test (Core.h).
 */

#include "base/Core.h"
#include "base/Version.h"

// newton-framework
#include "Objects.h"
#include "Pipes.h"
#include "Matt/MNP.h"

#include <sqlite3.h>

#include <cstring>
#include <mutex>
#include <vector>

extern "C" void InitObjectSystemCore(void);
void FlattenRef(RefArg inRef, CPipe & inPipe);
long FlattenRefSize(RefArg inRef);
Ref UnflattenRef(CPipe & inPipe);

namespace ncx {

const char * Version()
{
  return NCX_VERSION;
}


void InitCore()
{
  static std::once_flag once;
  std::call_once(once, [] { InitObjectSystemCore(); });
}


namespace {

void Report(std::string & ioReport, bool & ioAllOk, bool inOk, const std::string & inWhat)
{
  ioReport += (inOk ? "ok      " : "FAIL    ") + inWhat + "\n";
  ioAllOk = ioAllOk && inOk;
}


/** A frame as NSOF. */
std::vector<char> Flatten(RefArg inObj)
{
  long size = FlattenRefSize(inObj);
  std::vector<char> bytes(size);
  CPtrPipe out;
  out.init(bytes.data(), size, false, nullptr);
  FlattenRef(inObj, out);
  return bytes;
}


Ref Unflatten(std::vector<char> & inBytes)
{
  CPtrPipe in;
  in.init(inBytes.data(), (long)inBytes.size(), false, nullptr);
  return UnflattenRef(in);
}


bool IsTheFrame(RefArg inObj)
{
  return IsFrame(inObj) && ISINT(GetFrameSlot(inObj, MakeSymbol("answer")))
      && RINT(GetFrameSlot(inObj, MakeSymbol("answer"))) == 42;
}

} // namespace


bool SelfTest(std::string & outReport)
{
  outReport.clear();
  bool allOk = true;
  InitCore();

  // newton_ncx: NSOF there and back
  RefVar frame(AllocateFrame());
  SetFrameSlot(frame, MakeSymbol("answer"), MAKEINT(42));
  std::vector<char> nsof = Flatten(frame);
  bool nsofOk = false;
  newton_try
  {
    RefVar back(Unflatten(nsof));
    nsofOk = IsTheFrame(back);
  }
  newton_catch_all
  {
    nsofOk = false;
  }
  end_try;
  Report(outReport, allOk, nsofOk, "newton_ncx: a frame through NSOF (" + std::to_string(nsof.size()) + " bytes) and back");

  // newton_dock: an MNP frame there and back
  std::vector<uint8_t> header = { 2, kMNPLinkTransfer, 1 };
  std::vector<uint8_t> wire = MNPEncodeFrame(header, (const uint8_t *)nsof.data(), nsof.size());
  MNPDecoder decoder;
  bool mnpOk = false;
  for (uint8_t b : wire)
    if (decoder.feed(b) == MNPDecoder::kFrame)
      mnpOk = decoder.frame().size() == header.size() + nsof.size()
           && std::memcmp(decoder.frame().data() + header.size(), nsof.data(), nsof.size()) == 0;
  Report(outReport, allOk, mnpOk, "newton_dock: the NSOF in an MNP frame and back");

  // SQLite: the NSOF in a blob and back (as the .newtondevice files keep entries)
  bool sqlOk = false;
  sqlite3 * db = nullptr;
  if (sqlite3_open(":memory:", &db) == SQLITE_OK
   && sqlite3_exec(db, "CREATE TABLE entry (id INTEGER PRIMARY KEY, nsof BLOB)", nullptr, nullptr, nullptr) == SQLITE_OK)
  {
    sqlite3_stmt * insert = nullptr, * select = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT INTO entry (nsof) VALUES (?)", -1, &insert, nullptr) == SQLITE_OK
     && sqlite3_bind_blob(insert, 1, nsof.data(), (int)nsof.size(), SQLITE_TRANSIENT) == SQLITE_OK
     && sqlite3_step(insert) == SQLITE_DONE
     && sqlite3_prepare_v2(db, "SELECT nsof FROM entry", -1, &select, nullptr) == SQLITE_OK
     && sqlite3_step(select) == SQLITE_ROW)
    {
      const char * blob = (const char *)sqlite3_column_blob(select, 0);
      std::vector<char> back(blob, blob + sqlite3_column_bytes(select, 0));
      newton_try
      {
        sqlOk = IsTheFrame(RefVar(Unflatten(back)));
      }
      newton_catch_all
      {
        sqlOk = false;
      }
      end_try;
    }
    sqlite3_finalize(insert);
    sqlite3_finalize(select);
  }
  sqlite3_close(db);
  Report(outReport, allOk, sqlOk, std::string("SQLite ") + sqlite3_libversion() + ": the NSOF in a blob and back");

  return allOk;
}

} // namespace ncx
