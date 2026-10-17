/*
 File: NewtonText.h

 The Newton's styled text and RichText (PORTING.md 6.4): a paragraph's
 text with its styles (runs: [length, style, length, style, ...], the
 lengths in characters), a style being a font (a packed font spec: family,
 size, face; or a frame {family, face, size}), an ink word (handwriting
 kept as ink: drawn as a picture), or in Works a graphic ({class:
 'graphics, shape}: drawn as a picture).

 With the object system (the dock thread, or CallNewton).
 */

#ifndef NCX_TRANSLATORS_NEWTONTEXT_H
#define NCX_TRANSLATORS_NEWTONTEXT_H

#include "translators/RichText.h"

#include "Objects.h"

#include <functional>
#include <string>

namespace ncx {

/** A font spec as a style (inDefault: what a spec that isn't one gives). */
TextStyle StyleOfFontSpec(RefArg inSpec, const TextStyle & inDefault = TextStyle());
/** A style as a packed font spec (family + size << 10 + face << 20; a font
    of its own becomes its family's). */
int64_t FontSpecOfStyle(const TextStyle & inStyle);

/** A Newton string as UTF-16, its CRs as they are. */
std::u16string UTF16Of(RefArg inString);

/** Text with its styles added to a document: a CR (or LF) starts a
    paragraph, ink words and graphics become pictures. inStyles nil: all of
    it in inDefault. inParagraphStyle: the style of the paragraph starting
    at a character (Works' rulers); none: the paragraphs' as they are.
    inNewParagraph: the text starts a paragraph of its own (else it goes on
    the last one). */
void AddStyledText(RichDocument & ioDocument, const std::u16string & inText, RefArg inStyles, const TextStyle & inDefault,
                   const std::function<ParagraphStyle(size_t)> & inParagraphStyle = nullptr, bool inNewParagraph = true);

/** A paragraph's runs as Newton text: its text (paragraphs joined by CR)
    and styles array. */
void NewtonStyledText(const std::vector<RichParagraph> & inParagraphs, std::u16string & outText, RefVar & outStyles);

} // namespace ncx

#endif // NCX_TRANSLATORS_NEWTONTEXT_H
