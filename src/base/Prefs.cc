/*
 File: Prefs.cc

 The preferences on Fl_Preferences (Prefs.h).
 */

#include <FL/Fl_Preferences.H>

#include "base/Prefs.h"

#include <cstdlib>

namespace ncx {

struct Prefs::Store
{
  std::unique_ptr<Fl_Preferences> prefs;
};


Prefs::Prefs(std::unique_ptr<Store> inStore)
  : mStore(std::move(inStore))
{ }


Prefs::~Prefs()
{
  std::lock_guard<std::mutex> lock(mLock);
  mStore->prefs->flush();
}


Prefs & Prefs::App()
{
  static Prefs * app = [] {
    auto store = std::make_unique<Store>();
    store->prefs = std::make_unique<Fl_Preferences>(Fl_Preferences::USER_L, "newtonresearch.org", "Newton Connection");
    return new Prefs(std::move(store));     // never deleted: written at each flush() and by FLTK at exit
  }();
  return *app;
}


std::unique_ptr<Prefs> Prefs::InMemory(const std::string & inGroup)
{
  auto store = std::make_unique<Store>();
  store->prefs = std::make_unique<Fl_Preferences>((Fl_Preferences *)nullptr, inGroup.c_str());
  return std::unique_ptr<Prefs>(new Prefs(std::move(store)));
}


bool Prefs::has(const char * inKey)
{
  std::lock_guard<std::mutex> lock(mLock);
  return mStore->prefs->entryExists(inKey) != 0;
}


void Prefs::remove(const char * inKey)
{
  std::lock_guard<std::mutex> lock(mLock);
  mStore->prefs->deleteEntry(inKey);
}


bool Prefs::getBool(const char * inKey, bool inDefault)
{
  return getInt(inKey, inDefault ? 1 : 0) != 0;
}


int Prefs::getInt(const char * inKey, int inDefault)
{
  std::lock_guard<std::mutex> lock(mLock);
  int value = inDefault;
  mStore->prefs->get(inKey, value, inDefault);
  return value;
}


double Prefs::getDouble(const char * inKey, double inDefault)
{
  std::lock_guard<std::mutex> lock(mLock);
  double value = inDefault;
  mStore->prefs->get(inKey, value, inDefault);
  return value;
}


std::string Prefs::getString(const char * inKey, const std::string & inDefault)
{
  std::lock_guard<std::mutex> lock(mLock);
  char * value = nullptr;
  mStore->prefs->get(inKey, value, inDefault.c_str());
  std::string text = value ? value : inDefault;
  std::free(value);
  return text;
}


void Prefs::setBool(const char * inKey, bool inValue)
{
  setInt(inKey, inValue ? 1 : 0);
}


void Prefs::setInt(const char * inKey, int inValue)
{
  std::lock_guard<std::mutex> lock(mLock);
  mStore->prefs->set(inKey, inValue);
}


void Prefs::setDouble(const char * inKey, double inValue)
{
  std::lock_guard<std::mutex> lock(mLock);
  mStore->prefs->set(inKey, inValue);
}


void Prefs::setString(const char * inKey, const std::string & inValue)
{
  std::lock_guard<std::mutex> lock(mLock);
  mStore->prefs->set(inKey, inValue.c_str());
}


void Prefs::flush()
{
  std::lock_guard<std::mutex> lock(mLock);
  mStore->prefs->flush();
}


int Prefs::tcpPort()
{
  int port = getInt(pref::kTCPIPPort, 0);
  return (port > 0 && port < 65536) ? port : 3679;
}


int Prefs::baudRate()
{
  int bps = getInt(pref::kSerialBaud, 0);
  return bps > 0 ? bps : 38400;
}


int Prefs::logLevel()
{
  int level = getInt(pref::kLogLevel, 0);
  return level < 0 ? 0 : (level > 3 ? 3 : level);
}


bool Prefs::logToFile()
{
  return getBool(pref::kLogToFile, false);
}

} // namespace ncx
