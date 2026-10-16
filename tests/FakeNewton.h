/*
 File: FakeNewton.h

 A Newton for NCX's tests (PORTING.md 3.7): it connects to the desktop as
 Einstein's serial port does (a TCP client with MNP) or as a Newton with the
 Internet Enabler does (plain TCP: TCP takes MNP's place), docks, and
 answers the desktop's requests from a small fixture: one store, two soups
 with entries, a query's cursor, global functions. It runs until the
 desktop disconnects.

 The fixture:
   store    {name: "Internal", kind: "Internal", signature: 1234567,
             totalSize: 1048576, usedSize: 262144}
   soups    "Names" (signature 11), "Notes" (12)
   entries  Names: 100 {_uniqueId: 100, cardType: 0, name: {first: "Ada",
            last: "Lovelace"}}, 101 (Charles Babbage)
   GetUserConfig('userFont) -> 12290, Gestalt(...) -> {}
   lpkg     taken (its size remembered), dres 0
 */

#ifndef NCX_TESTS_FAKENEWTON_H
#define NCX_TESTS_FAKENEWTON_H

#include <cstdint>
#include <string>
#include <vector>

namespace ncx {

struct FakeNewtonOptions
{
  int port = 3679;
  bool plainTCP = false;              // the Internet Enabler: no MNP
  std::string password;               // the Newton's
  std::string name = "Ada Lovelace";  // its owner
  double connectSeconds = 10;         // keep trying to connect this long
};

class FakeNewton
{
public:
  /** Connect, dock, answer. True if it docked and the desktop disconnected
      ('disc') in the end; why() says what went wrong otherwise. */
  bool run(const FakeNewtonOptions & inOptions);

  const std::string & why() const { return mWhy; }
  /** The commands the desktop sent, in order ("dock", "dinf", ...). */
  const std::vector<std::string> & received() const { return mReceived; }
  /** The size of the last package the desktop sent (lpkg). */
  size_t packageSize() const { return mPackageSize; }

private:
  std::string mWhy;
  std::vector<std::string> mReceived;
  size_t mPackageSize = 0;
};

} // namespace ncx

#endif // NCX_TESTS_FAKENEWTON_H
