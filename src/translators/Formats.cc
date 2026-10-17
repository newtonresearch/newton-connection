/*
 File: Formats.cc

 The translators NCX has: what the Cocoa app read from its plug-ins'
 Info.plists (NCExportFormats, NCImportFormats) and registered itself
 (*package*, text, *default*). One table, not a registration at static
 initialisation: in a static library the linker would drop the
 translators nobody refers to.
 */

#include "translators/Translator.h"

namespace ncx {

std::unique_ptr<Exporter> MakeTextExporter();
std::unique_ptr<Exporter> MakePackageExporter();
std::unique_ptr<Exporter> MakeImageExporter();
std::unique_ptr<Exporter> MakeNameToVCard();
std::unique_ptr<Importer> MakeNameFromVCard();
std::unique_ptr<Exporter> MakeDateToICal();
std::unique_ptr<Importer> MakeDateFromICal();
std::unique_ptr<Importer> MakeNoteFromText();


const std::vector<ExportFormat> & ExportFormats()
{
  static const std::vector<ExportFormat> formats = {
    { "package", "*package*", "newtonpkg", "Newton package", MakePackageExporter },
    { "text", "text", "text", "Text", MakeTextExporter },
    { "image", "*image*", "png", "PNG image", MakeImageExporter },
    { "vcard", "person", "vcf", "vCard", MakeNameToVCard },
    { "vcard", "owner", "vcf", "vCard", MakeNameToVCard },
    { "vcard", "company", "vcf", "vCard", MakeNameToVCard },
    { "icalendar", "meeting", "ics", "iCalendar", MakeDateToICal },
  };
  return formats;
}


const std::vector<ImportFormat> & ImportFormats()
{
  static const std::vector<ImportFormat> formats = {
    { "Names", { "vcf", "vcard" }, "Names", "vCard", MakeNameFromVCard },
    { "Dates", { "ics", "ical", "ifb", "icalendar" }, "Calendar", "iCalendar", MakeDateFromICal },
    { "Notes", { "txt", "text" }, "Notes", "Text", MakeNoteFromText },
  };
  return formats;
}

} // namespace ncx
