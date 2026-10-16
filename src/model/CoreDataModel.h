/*
 File: CoreDataModel.h

 What makes an SQLite file a Core Data store of the Cocoa app's model
 (NCX/Store.xcdatamodel; PORTING.md 1.5, decision D5): the tables, the
 entities' numbers, and the metadata Core Data checks when it opens a store
 (the model's version hashes). New .newtondevice files get these, so the
 macOS Core Data NCX opens them too.

 The hashes are the model's (xcrun momc NCX/Store.xcdatamodel gives the
 same, entityVersionHashesByName); they were copied from a store the Cocoa
 app wrote.
 */

#ifndef NCX_MODEL_COREDATAMODEL_H
#define NCX_MODEL_COREDATAMODEL_H

#include "model/Plist.h"

#include <string>
#include <vector>

namespace ncx {
namespace coredata {

/** The entities, in Core Data's order (their numbers are 1, 2, ...): App,
    Device, Entry, Soup, Store, UserInfo. */
const std::vector<std::string> & EntityNames();

/** The statements that make the tables and indexes, as Core Data does. */
const std::vector<std::string> & Schema();

/** New store's metadata (Z_METADATA.Z_PLIST): the model's hashes and the
    Newton's id and name (NCX's own keys). */
Plist MakeMetadata(const std::string & inNewtonId, const std::string & inNewtonName);

/** True if a store's metadata says it is of this model (its hashes are). */
bool IsThisModel(const Plist & inMetadata);

/** A new random UUID, as Core Data writes them (upper case). */
std::string MakeUUID();

} // namespace coredata
} // namespace ncx

#endif // NCX_MODEL_COREDATAMODEL_H
