/*
 File: Render.h

 Newton graphics as pixels (PORTING.md 6.3; the Cocoa app's renderShapes
 of NoteToRTF and NCImageTranslator), drawn by newton-framework's QuickDraw
 on a Canvas:
   - the graphics of a note: its paragraphs with ink (an ink slot:
     InkDrawInRect), pictures ('pict: their icon) and polygons ('poly: the
     points of NCX's ink), in one image cropped to where they are
   - a bitmap (an image entry's image, an icon): its bits and their bounds

 With the object system (the dock thread, or CallNewton).
 */

#ifndef NCX_TRANSLATORS_RENDER_H
#define NCX_TRANSLATORS_RENDER_H

#include "translators/Canvas.h"

#include "Objects.h"

namespace ncx {

/** The graphics among a note's paragraphs (data, or a graph paper's
    editView), cropped to them with a margin of 2 pixels; empty if there
    are none. outLeft, outTop: where the image's corner is on the note. */
Pixels RenderGraphics(RefArg inParagraphs, int * outLeft = nullptr, int * outTop = nullptr);

/** A paragraph with graphics (ink, 'pict, 'poly). */
bool IsGraphicParagraph(RefArg inParagraph);

/** A bitmap frame ({bits, bounds}, a NewtPaint image, an icon) at its own
    size; empty if it isn't one. */
Pixels RenderBitmap(RefArg inBitmap);

/** A shape, or an array of shapes and the style frames before them (a
    Works drawing's shapes, a graphic in Works text), as drawn by the
    framework's DrawShape, cropped to them with a margin of 2 pixels; empty
    if there is nothing to draw. */
Pixels RenderShapes(RefArg inShapes);

/** An ink word (a text style of class 'inkWord: handwriting kept as ink
    in a paragraph) at its size, a margin of 2 around; empty if it isn't
    one. outBaseline: the baseline, from the top. */
Pixels RenderInkWord(RefArg inInkWord, int * outBaseline = nullptr);

} // namespace ncx

#endif // NCX_TRANSLATORS_RENDER_H
