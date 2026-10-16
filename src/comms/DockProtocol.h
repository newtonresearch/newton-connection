/*
 File: DockProtocol.h

 The Newton Docking Protocol (from the Cocoa app's DockProtocol.h and
 DockErrors.h): commands, the Newton's info block, session types, icons,
 errors. A command is 4 characters as a big-endian 32-bit number:
 Cmd("rtdk") == 0x7274646B (the Cocoa app wrote 'rtdk', which compilers
 handle differently).

 On the wire an event is 'newt' 'dock', the command, the length of the data
 (kIndeterminateLength: until the next 'newt' 'dock'), the data, padded
 with zeros to a multiple of 4 bytes. All numbers big-endian.
 */

#ifndef NCX_COMMS_DOCKPROTOCOL_H
#define NCX_COMMS_DOCKPROTOCOL_H

#include <cstdint>
#include <string>

namespace ncx {

typedef uint32_t EventType;

/** "rtdk" -> 0x7274646B */
constexpr EventType Cmd(const char (&inName)[5])
{
  return (uint32_t)(uint8_t)inName[0] << 24 | (uint32_t)(uint8_t)inName[1] << 16
       | (uint32_t)(uint8_t)inName[2] << 8 | (uint32_t)(uint8_t)inName[3];
}

/** 0x7274646B -> "rtdk" (other bytes as '.') */
std::string CmdName(EventType inCommand);

inline constexpr EventType kNewtEventClass = Cmd("newt");
inline constexpr EventType kDockEventId    = Cmd("dock");
inline constexpr uint32_t kIndeterminateLength = 0xFFFFFFFF;

inline constexpr uint32_t kBaseProtocolVersion  = 9;
inline constexpr uint32_t kDanteProtocolVersion = 10;

/** Seconds without traffic before the desktop says 'helo' (the tickler)
    and before a reply is given up on. */
inline constexpr int kDefaultTimeout = 30;

/** A command that matches any reply (Session::receive). */
inline constexpr EventType kDAnyEvent = 0;

// Commands

// Starting a Session
// Newton -> Desktop
inline constexpr EventType kDRequestToAutoDock           = Cmd("auto");
inline constexpr EventType kDRequestToDock               = Cmd("rtdk");
inline constexpr EventType kDNewtonName                  = Cmd("name");  // + NewtonInfo + UniChar[] name
inline constexpr EventType kDNewtonInfo                  = Cmd("ninf");  // + protocol version + encrypted key
inline constexpr EventType kDPassword                    = Cmd("pass");  // + encrypted key
// Desktop -> Newton
inline constexpr EventType kDInitiateDocking             = Cmd("dock");
inline constexpr EventType kDSetTimeout                  = Cmd("stim");  // + timeout in seconds
inline constexpr EventType kDWhichIcons                  = Cmd("wicn");  // + bit mask
inline constexpr EventType kDDesktopInfo                 = Cmd("dinf");  // + desktop info
inline constexpr EventType kDPWWrong                     = Cmd("pwbd");

// Desktop <- -> Newton
inline constexpr EventType kDResult                      = Cmd("dres");  // + error code
inline constexpr EventType kDDisconnect                  = Cmd("disc");
inline constexpr EventType kDHello                       = Cmd("helo");
inline constexpr EventType kDTest                        = Cmd("test");
inline constexpr EventType kDRefTest                     = Cmd("rtst");
inline constexpr EventType kDUnknownCommand              = Cmd("unkn");

// Desktop -> Newton
inline constexpr EventType kDLastSyncTime                = Cmd("stme");  // + time in minutes
inline constexpr EventType kDGetStoreNames               = Cmd("gsto");  // + array ref of store info
inline constexpr EventType kDGetSoupNames                = Cmd("gets");
inline constexpr EventType kDSetCurrentStore             = Cmd("ssto");
inline constexpr EventType kDSetCurrentSoup              = Cmd("ssou");
inline constexpr EventType kDGetSoupIDs                  = Cmd("gids");
inline constexpr EventType kDGetChangedIDs               = Cmd("gcid");
inline constexpr EventType kDDeleteEntries               = Cmd("dele");
inline constexpr EventType kDAddEntry                    = Cmd("adde");
inline constexpr EventType kDChangedEntry                = Cmd("cent");
inline constexpr EventType kDReturnEntry                 = Cmd("rete");
inline constexpr EventType kDReturnChangedEntry          = Cmd("rcen");
inline constexpr EventType kDEmptySoup                   = Cmd("esou");
inline constexpr EventType kDDeleteSoup                  = Cmd("dsou");
inline constexpr EventType kDGetIndexDescription         = Cmd("gind");
inline constexpr EventType kDCreateSoup                  = Cmd("csop");
inline constexpr EventType kDGetSoupInfo                 = Cmd("gsin");
inline constexpr EventType kDSetSoupInfo                 = Cmd("sinf");
// Newton 1 protocol
inline constexpr EventType kDGetPackageIDs               = Cmd("gpid");
inline constexpr EventType kDBackupPackages              = Cmd("bpkg");
inline constexpr EventType kDDeleteAllPackages           = Cmd("dpkg");
inline constexpr EventType kDDeletePkgDir                = Cmd("dpkd");
inline constexpr EventType kDGetInheritance              = Cmd("ginh");
inline constexpr EventType kDGetPatches                  = Cmd("gpat");
inline constexpr EventType kDRestorePatch                = Cmd("rpat");

// Newton -> Desktop
inline constexpr EventType kDCurrentTime                 = Cmd("time");
inline constexpr EventType kDStoreNames                  = Cmd("stor");
inline constexpr EventType kDSoupNames                   = Cmd("soup");
inline constexpr EventType kDSoupIDs                     = Cmd("sids");
inline constexpr EventType kDSoupInfo                    = Cmd("sinf");
inline constexpr EventType kDIndexDescription            = Cmd("indx");
inline constexpr EventType kDChangedIDs                  = Cmd("cids");
inline constexpr EventType kDAddedID                     = Cmd("adid");
inline constexpr EventType kDEntry                       = Cmd("entr");
// Newton 1 protocol
inline constexpr EventType kDPackageIdList               = Cmd("pids");
inline constexpr EventType kDPackage                     = Cmd("apkg");
inline constexpr EventType kDInheritance                 = Cmd("dinh");
inline constexpr EventType kDPatches                     = Cmd("patc");

// Load Package
// Newton -> Desktop
inline constexpr EventType kDLoadPackageFile             = Cmd("lpfl");
// Desktop -> Newton
inline constexpr EventType kDLoadPackage                 = Cmd("lpkg");

// Remote Query
// Desktop -> Newton
inline constexpr EventType kDQuery                       = Cmd("qury");
inline constexpr EventType kDCursorGotoKey               = Cmd("goto");
inline constexpr EventType kDCursorMap                   = Cmd("cmap");
inline constexpr EventType kDCursorEntry                 = Cmd("crsr");
inline constexpr EventType kDCursorMove                  = Cmd("move");
inline constexpr EventType kDCursorNext                  = Cmd("next");
inline constexpr EventType kDCursorPrev                  = Cmd("prev");
inline constexpr EventType kDCursorReset                 = Cmd("rset");
inline constexpr EventType kDCursorResetToEnd            = Cmd("rend");
inline constexpr EventType kDCursorWhichEnd              = Cmd("whch");
inline constexpr EventType kDCursorCountEntries          = Cmd("cnt ");
inline constexpr EventType kDCursorFree                  = Cmd("cfre");
// Newton -> Desktop
inline constexpr EventType kDLongData                    = Cmd("ldta");
inline constexpr EventType kDRefResult                   = Cmd("ref ");

// Keyboard Passthrough
// Desktop <- -> Newton
inline constexpr EventType kDStartKeyboardPassthrough    = Cmd("kybd");
// Desktop -> Newton
inline constexpr EventType kDKeyboardChar                = Cmd("kbdc");  // + UniChar + flags
inline constexpr EventType kDKeyboardString              = Cmd("kbds");  // + UniChar[]

// Misc additions
// Newton -> Desktop
inline constexpr EventType kDDefaultStore                = Cmd("dfst");
inline constexpr EventType kDAppNames                    = Cmd("appn");
inline constexpr EventType kDImportParameterSlipResult   = Cmd("islr");
inline constexpr EventType kDPackageInfo                 = Cmd("pinf");
inline constexpr EventType kDSetBaseID                   = Cmd("base");
inline constexpr EventType kDBackupIDs                   = Cmd("bids");
inline constexpr EventType kDBackupSoupDone              = Cmd("bsdn");
inline constexpr EventType kDSoupNotDirty                = Cmd("ndir");
inline constexpr EventType kDSynchronize                 = Cmd("sync");
inline constexpr EventType kDCallResult                  = Cmd("cres");

// Desktop -> Newton
inline constexpr EventType kDRemovePackage               = Cmd("rmvp");
inline constexpr EventType kDResultString                = Cmd("ress");
inline constexpr EventType kDSourceVersion               = Cmd("sver");
inline constexpr EventType kDAddEntryWithUniqueID        = Cmd("auni");
inline constexpr EventType kDGetPackageInfo              = Cmd("gpin");
inline constexpr EventType kDGetDefaultStore             = Cmd("gdfs");
inline constexpr EventType kDCreateDefaultSoup           = Cmd("cdsp");
inline constexpr EventType kDGetAppNames                 = Cmd("gapp");
inline constexpr EventType kDRegProtocolExtension        = Cmd("pext");
inline constexpr EventType kDRemoveProtocolExtension     = Cmd("rpex");
inline constexpr EventType kDSetStoreSignature           = Cmd("ssig");
inline constexpr EventType kDSetSoupSignature            = Cmd("ssos");
inline constexpr EventType kDImportParametersSlip        = Cmd("islp");
inline constexpr EventType kDGetPassword                 = Cmd("gpwd");
inline constexpr EventType kDSendSoup                    = Cmd("snds");
inline constexpr EventType kDBackupSoup                  = Cmd("bksp");
inline constexpr EventType kDSetStoreName                = Cmd("ssna");
inline constexpr EventType kDCallGlobalFunction          = Cmd("cgfn");
inline constexpr EventType kDCallRootMethod              = Cmd("crmf");  // spec says 'crmd'!
inline constexpr EventType kDSetVBOCompression           = Cmd("cvbo");
// kDRestorePatch: as above (the Cocoa header defined it twice)

// Desktop <- -> Newton
inline constexpr EventType kDOperationDone               = Cmd("opdn");
inline constexpr EventType kDOperationCanceled           = Cmd("opca");  // spec says 'opcn'!
inline constexpr EventType kDOpCanceledAck               = Cmd("ocaa");

// Sync and Selective Sync
// Newton -> Desktop
inline constexpr EventType kDRequestToSync               = Cmd("ssyn");
inline constexpr EventType kDSyncOptions                 = Cmd("sopt");

// Desktop -> Newton
inline constexpr EventType kDGetSyncOptions              = Cmd("gsyn");
inline constexpr EventType kDSyncResults                 = Cmd("sres");
inline constexpr EventType kDSetStoreGetNames            = Cmd("ssgn");
inline constexpr EventType kDSetSoupGetInfo              = Cmd("ssgi");
inline constexpr EventType kDGetChangedIndex             = Cmd("cidx");
inline constexpr EventType kDGetChangedInfo              = Cmd("cinf");

// File browsing
// Newton -> Desktop
inline constexpr EventType kDRequestToBrowse             = Cmd("rtbr");
inline constexpr EventType kDGetDevices                  = Cmd("gdev");  // Windows only
inline constexpr EventType kDGetDefaultPath              = Cmd("dpth");
inline constexpr EventType kDGetFilesAndFolders          = Cmd("gfil");
inline constexpr EventType kDSetPath                     = Cmd("spth");
inline constexpr EventType kDGetFileInfo                 = Cmd("gfin");
inline constexpr EventType kDInternalStore               = Cmd("isto");
inline constexpr EventType kDResolveAlias                = Cmd("rali");
inline constexpr EventType kDGetFilters                  = Cmd("gflt");  // Windows only
inline constexpr EventType kDSetFilter                   = Cmd("sflt");  // Windows only
inline constexpr EventType kDSetDrive                    = Cmd("sdrv");  // Windows only

// Desktop -> Newton
inline constexpr EventType kDDevices                     = Cmd("devs");  // Windows only
inline constexpr EventType kDFilters                     = Cmd("filt");  // Windows only
inline constexpr EventType kDPath                        = Cmd("path");
inline constexpr EventType kDFilesAndFolders             = Cmd("file");
inline constexpr EventType kDFileInfo                    = Cmd("finf");
inline constexpr EventType kDGetInternalStore            = Cmd("gist");
inline constexpr EventType kDAliasResolved               = Cmd("alir");

// File importing
// Newton -> Desktop
inline constexpr EventType kDImportFile                  = Cmd("impt");
inline constexpr EventType kDSetTranslator               = Cmd("tran");

// Desktop -> Newton
inline constexpr EventType kDTranslatorList              = Cmd("trnl");
inline constexpr EventType kDImporting                   = Cmd("dimp");
inline constexpr EventType kDSoupsChanged                = Cmd("schg");
inline constexpr EventType kDSetStoreToDefault           = Cmd("sdef");

// Restore originated on Newton
// Newton -> Desktop
inline constexpr EventType kDRestoreFile                 = Cmd("rsfl");
inline constexpr EventType kDGetRestoreOptions           = Cmd("grop");
inline constexpr EventType kDRestoreAll                  = Cmd("rall");

// Desktop <- -> Newton
inline constexpr EventType kDRestoreOptions              = Cmd("ropt");
inline constexpr EventType kDRestorePackage              = Cmd("rpkg");

// Desktop Initiated Functions while connected
// Desktop  -> Newton
inline constexpr EventType kDDesktopInControl            = Cmd("dsnc");
//#define kDRequestToSync				'ssyn'
inline constexpr EventType kDRequestToRestore            = Cmd("rrst");
inline constexpr EventType kDRequestToInstall            = Cmd("rins");

// Newton 2.1
inline constexpr EventType kDSetStatusText               = Cmd("stxt");



/** The Newton's info block, sent with kDNewtonName (host byte order here). */
struct NewtonInfo
{
  uint32_t fNewtonID = 0;          // a unique id of the Newton
  uint32_t fManufacturer = 0;
  uint32_t fMachineType = 0;       // the hardware
  uint32_t fROMVersion = 0;        // major in the high 16 bits, minor in the low
  uint32_t fROMStage = 0;          // language and stage (alpha, beta, final)
  uint32_t fRAMSize = 0;
  uint32_t fScreenHeight = 0;
  uint32_t fScreenWidth = 0;
  uint32_t fPatchVersion = 0;      // 0: unpatched
  uint32_t fNOSVersion = 0;
  uint32_t fInternalStoreSig = 0;
  uint32_t fScreenResolutionV = 0;
  uint32_t fScreenResolutionH = 0;
  uint32_t fScreenDepth = 0;
  uint32_t fSystemFlags = 0;       // 1: has a serial number, 2: has a target protocol
  uint32_t fSerialNumber[2] = { 0, 0 };
  uint32_t fTargetProtocol = 0;
};

enum DesktopType { kMacDesktop, kWindowsDesktop };

enum SessionType
{
  kNoSession, kSettingUpSession, kSynchronizeSession, kRestoreSession, kLoadPackageSession,
  kTestCommSession, kLoadPatchSession, kUpdatingStoresSession
};

enum IconMask
{
  kBackupIcon = 1, kRestoreIcon = 1 << 1, kInstallIcon = 1 << 2, kImportIcon = 1 << 3,
  kSyncIcon = 1 << 4, kKeyboardIcon = 1 << 5
};

enum DesktopFileType { kDesktop, kDesktopFile, kDesktopFolder, kDesktopDisk };
enum DesktopDriveType { kFloppyDrive, kHardDrive, kCDROMDrive, kNetDrive };
enum AppNamesInfo { kNamesAndSoupsForAllStores, kNamesAndSoupsForThisStore, kNamesForAllStores, kNamesForThisStore };
enum RestoreTarget { kRestoreToNewton, kRestoreToCard };
enum VBOCompression { kUncompressedVBOs, kCompressedPackagesOnly, kCompressedVBOs };
enum SourceVersion { kOnePointXData = 1, kTwoPointXData = 2 };

/** The Newton's error numbers for the dock (DockErrors.h). */
enum DockError
{
  kDockErrBadStoreSignature = -28001,
  kDockErrBadEntry = -28002,
  kDockErrAborted = -28003,
  kDockErrBadQuery = -28004,
  kDockErrReadEntryError = -28005,
  kDockErrBadCurrentSoup = -28006,
  kDockErrBadCommandLength = -28007,
  kDockErrEntryNotFound = -28008,
  kDockErrBadConnection = -28009,
  kDockErrFileNotFound = -28010,
  kDockErrIncompatibleProtocol = -28011,
  kDockErrProtocolError = -28012,
  kDockErrDockingCanceled = -28013,
  kDockErrStoreNotFound = -28014,
  kDockErrSoupNotFound = -28015,
  kDockErrBadHeader = -28016,
  kDockErrOutOfMemory = -28017,
  kDockErrNewtonVersionTooNew = -28018,
  kDockErrPackageCantLoad = -28019,
  kDockErrProtocolExtAlreadyRegistered = -28020,
  kDockErrRemoteImportError = -28021,
  kDockErrBadPasswordError = -28022,
  kDockErrRetryPW = -28023,
  kDockErrIdleTooLong = -28024,
  kDockErrOutOfPower = -28025,
  kDockErrBadCursor = -28026,
  kDockErrAlreadyBusy = -28027,
  kDockErrDesktopError = -28028,
  kDockErrCantConnectToModem = -28029,
  kDockErrDisconnected = -28030,
  kDockErrAccessDenied = -28031,
  kDockErrDisconnectDuringRead = -28100,
  kDockErrReadFailed = -28101,
  kDockErrCommunicationsToolNotFound = -28102,
  kDockErrInvalidModemToolVersion = -28103,
  kDockErrCardNotInstalled = -28104,
  kDockErrBrowserFileNotFound = -28105,
  kDockErrBrowserVolumeNotFound = -28106,
  kDockErrBrowserPathNotFound = -28107,
};

} // namespace ncx

#endif // NCX_COMMS_DOCKPROTOCOL_H
