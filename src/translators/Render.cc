/*
 File: Render.cc

 Newton graphics as pixels (Render.h).
 */

#include "translators/Render.h"

#include "RSSymbols.h"
#include "DrawShape.h"
#include "Geometry.h"
#include "Ink.h"
#include "QDHost.h"
#include "ViewFlags.h"
#include "Iterators.h"

#include <algorithm>
#include <vector>

namespace ncx {

namespace {

/** A bounds object (a frame, or an 8-byte binary: top, left, bottom,
    right, big-endian) as a Rect; false if it isn't one. */
bool BoundsOf(RefArg inBounds, Rect & outRect)
{
  if (IsFrame(inBounds))
    return FromObject(inBounds, &outRect);
  if (IsBinary(inBounds) && Length(inBounds) >= 8) {
    const uint8_t * p = (const uint8_t *)BinaryData(inBounds);
    outRect.top = (short)(p[0] << 8 | p[1]);
    outRect.left = (short)(p[2] << 8 | p[3]);
    outRect.bottom = (short)(p[4] << 8 | p[5]);
    outRect.right = (short)(p[6] << 8 | p[7]);
    return true;
  }
  return false;
}

bool IsEmpty(const Rect & inRect)
{
  return inRect.right <= inRect.left || inRect.bottom <= inRect.top;
}

/** A copy in which nothing is shared: every frame, array and binary its
    own (MungeBounds turns a binary's byte order in place, and NSOF shares
    an object that is in two places, as a bitmap's bits copied with it). */
Ref UnsharedCopy(RefArg inObject, int inDepth = 0)
{
  if (inDepth > 32 || !ISREALPTR(inObject))
    return inObject;
  if (IsSymbol(inObject))
    return inObject;
  if (IsBinary(inObject))
    return Clone(inObject);
  RefVar copy(Clone(inObject));
  if (IsArray(inObject)) {
    for (ArrayIndex i = 0, n = Length(copy); i < n; ++i)
      SetArraySlot(copy, i, RefVar(UnsharedCopy(RefVar(GetArraySlot(copy, i)), inDepth + 1)));
  } else if (IsFrame(inObject)) {
    std::vector<std::pair<RefStruct, RefStruct>> slots;
    CObjectIterator iter(copy);
    for (; !iter.done(); iter.next())
      slots.emplace_back(RefStruct(iter.tag()), RefStruct(iter.value()));
    for (auto & slot : slots)
      SetFrameSlot(copy, slot.first, RefVar(UnsharedCopy(slot.second, inDepth + 1)));
  }
  return copy;
}

/** NCX's ink polygon ('poly's points: 0, the number of points, the points
    as v, h; big-endian) from inLeft, inTop, inPen pixels wide (the
    framework's DrawPolygon always draws 2). */
void StrokePolygon(RefArg inPoints, int inLeft, int inTop, int inPen)
{
  const uint8_t * p = (const uint8_t *)BinaryData(inPoints);
  size_t length = Length(inPoints);
  size_t count = (size_t)(p[2] << 8 | p[3]);
  if (count < 2 || 4 + count * 4 > length)
    return;
  std::vector<float> xy(2 * count);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t * point = p + 4 + i * 4;
    xy[2 * i + 1] = (float)(inTop + (short)(point[0] << 8 | point[1]));
    xy[2 * i] = (float)(inLeft + (short)(point[2] << 8 | point[3]));
  }
  QDHostSaveState();
  QDHostSetLineWidth((float)inPen);
  QDHostStrokePolyline(xy.data(), count, false);
  QDHostRestoreState();
}

} // namespace


bool IsGraphicParagraph(RefArg inParagraph)
{
  if (!IsFrame(inParagraph))
    return false;
  RefVar stationery(GetFrameSlot(inParagraph, SYMA(viewStationery)));
  return NOTNIL(GetFrameSlot(inParagraph, SYMA(ink))) || EQRef(stationery, MakeSymbol("pict"))
         || EQRef(stationery, MakeSymbol("poly"));
}


Pixels RenderGraphics(RefArg inParagraphs, int * outLeft, int * outTop)
{
  // the bounds of all the graphics
  Rect all;
  SetEmptyRect(&all);
  for (ArrayIndex i = 0, n = IsArray(inParagraphs) ? Length(inParagraphs) : 0; i < n; ++i) {
    RefVar para(GetArraySlot(inParagraphs, i));
    Rect bounds;
    if (IsGraphicParagraph(para) && BoundsOf(RefVar(GetFrameSlot(para, SYMA(viewBounds))), bounds) && !IsEmpty(bounds))
      UnionRect(&all, &bounds, &all);
  }
  if (IsEmpty(all))
    return Pixels();

  // drawn where they are on the note, then cropped
  const int kMargin = 2;
  int width = all.right + kMargin, height = all.bottom + kMargin;
  Pixels pixels;
  {
    Canvas canvas(width, height);
    Point zero = { 0, 0 };
    for (ArrayIndex i = 0, n = Length(inParagraphs); i < n; ++i) {
      RefVar para(GetArraySlot(inParagraphs, i));
      if (!IsGraphicParagraph(para))
        continue;
      Rect bounds;
      if (!BoundsOf(RefVar(GetFrameSlot(para, SYMA(viewBounds))), bounds))
        continue;
      // the pen as the paragraph has it (viewFormat: vfPen(n)), else 2
      // for ink (the Cocoa app's) and 1 for the rest
      RefVar format(GetFrameSlot(para, SYMA(viewFormat)));
      int pen = ISINT(format) ? (int)((RINT(format) & vfPenMask) >> vfPenShift) : 0;
      bool isInk = NOTNIL(GetFrameSlot(para, SYMA(ink)));
      if (pen == 0)
        pen = isInk ? 2 : 1;
      SetLineWidth((short)pen);
      newton_try
      {
        RefVar stationery(GetFrameSlot(para, SYMA(viewStationery)));
        RefVar ink(GetFrameSlot(para, SYMA(ink)));
        if (NOTNIL(ink)) {
          InkDrawInRect(ink, (size_t)pen, &bounds, &bounds, false);
        } else if (EQRef(stationery, MakeSymbol("pict"))) {
          RefVar icon(GetFrameSlot(para, SYMA(icon)));
          bool isBitmap = IsFrame(icon) && (FrameHasSlot(icon, SYMA(bits)) || FrameHasSlot(icon, MakeSymbol("colorData")));
          if (isBitmap && IsFrame(GetFrameSlot(icon, SYMA(bounds))))
            DrawPicture(icon, &bounds, 0, modeCopy);          // an icon: its bounds at the paragraph's corner
          else if (isBitmap)
            DrawBitmap(icon, &bounds, modeCopy);              // bits only: into the paragraph's bounds
          else if (NOTNIL(icon))
            DrawShape(icon, RA(NILREF), zero);
        } else {
          RefVar points(GetFrameSlot(para, MakeSymbol("points")));
          if (IsBinary(points) && Length(points) >= 4)
            StrokePolygon(points, bounds.left, bounds.top, pen);
        }
      }
      newton_catch_all
      {
        // a graphic that can't be drawn is left out
      }
      end_try;
    }
    SetLineWidth(1);
    pixels = canvas.take();
  }
  int left = std::max(0, all.left - kMargin), top = std::max(0, all.top - kMargin);
  if (outLeft)
    *outLeft = left;
  if (outTop)
    *outTop = top;
  return Crop(pixels, left, top, width, height);
}


Pixels RenderBitmap(RefArg inBitmap)
{
  if (!IsFrame(inBitmap))
    return Pixels();
  RefVar bits(GetFrameSlot(inBitmap, SYMA(bits)));
  RefVar color(GetFrameSlot(inBitmap, MakeSymbol("colorData")));
  if (!IsBinary(bits) && !NOTNIL(color))
    return Pixels();
  // its bounds: a bounds slot, or those in its bits (a FramBitmap: the
  // base address, row bytes, a short, the bounds)
  Rect bounds;
  bool known = BoundsOf(RefVar(GetFrameSlot(inBitmap, SYMA(bounds))), bounds);
  if (!known && IsBinary(bits) && Length(bits) >= 16) {
    const uint8_t * p = (const uint8_t *)BinaryData(bits) + 8;
    bounds.top = (short)(p[0] << 8 | p[1]);
    bounds.left = (short)(p[2] << 8 | p[3]);
    bounds.bottom = (short)(p[4] << 8 | p[5]);
    bounds.right = (short)(p[6] << 8 | p[7]);
    known = true;
  }
  if (!known || IsEmpty(bounds) || bounds.right - bounds.left > 4096 || bounds.bottom - bounds.top > 4096)
    return Pixels();
  Rect at;
  at.left = 0;
  at.top = 0;
  at.right = bounds.right - bounds.left;
  at.bottom = bounds.bottom - bounds.top;
  Canvas canvas(at.right, at.bottom);
  bool drawn = true;
  newton_try
  {
    DrawBitmap(inBitmap, &at, modeCopy);
  }
  newton_catch_all
  {
    drawn = false;
  }
  end_try;
  Pixels pixels = canvas.take();
  return drawn ? pixels : Pixels();
}



Pixels RenderInkWord(RefArg inInkWord, int * outBaseline)
{
  if (!IsInkWord(inInkWord) || Length(inInkWord) < 16)
    return Pixels();
  // its size: the last 8 bytes, big-endian (the ROM's PackedInkWordInfo;
  // the framework's ExpandPackedInkWordInfo is not done yet): width:10,
  // height:10, descent:10, pen - 1:2; font size:10, scale:16 (8.8),
  // face:6. The strokes fill width by height + descent; in the text the
  // word is drawn at its scale.
  const uint8_t * p = (const uint8_t *)BinaryData(inInkWord) + Length(inInkWord) - 8;
  uint32_t w0 = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
  uint32_t w1 = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
  int inkWidth = (int)(w0 >> 22), inkHeight = (int)((w0 >> 12) & 0x3FF), inkDescent = (int)((w0 >> 2) & 0x3FF);
  int pen = (int)(w0 & 3) + 1;
  float scale = ((w1 >> 6) & 0xFFFF) / 256.0f;
  if (scale <= 0)
    scale = 1;
  int width = (int)(inkWidth * scale + 0.5f), height = (int)((inkHeight + inkDescent) * scale + 0.5f);
  if (width <= 0 || height <= 0 || width > 2000 || height > 2000)
    return Pixels();
  const int kMargin = 2;
  Rect original, at;
  SetRect(&original, 0, 0, (short)inkWidth, (short)(inkHeight + inkDescent));
  SetRect(&at, kMargin, kMargin, (short)(kMargin + width), (short)(kMargin + height));
  Canvas canvas(width + 2 * kMargin, height + 2 * kMargin);
  bool drawn = true;
  newton_try
  {
    InkDrawInRect(inInkWord, (size_t)pen, &original, &at, false);
  }
  newton_catch_all
  {
    drawn = false;
  }
  end_try;
  Pixels pixels = canvas.take();
  if (outBaseline)
    *outBaseline = kMargin + (int)(inkHeight * scale + 0.5f);
  return drawn ? pixels : Pixels();
}


Pixels RenderShapes(RefArg inShapes)
{
  if (ISNIL(inShapes))
    return Pixels();
  // a copy in the host's byte order (the shapes' rectangles and points are
  // big-endian, as the Newton has them; MungeBounds turns them in place)
  RefVar shapes;
  Rect bounds;
  bool measured = true;
  newton_try
  {
    shapes = UnsharedCopy(inShapes);
#if defined(hasByteSwapping)
    MungeBounds(shapes);
#endif
    ShapeBounds(shapes, &bounds);
  }
  newton_catch_all
  {
    measured = false;
  }
  end_try;
  if (!measured || IsEmpty(bounds) || bounds.right > 4096 || bounds.bottom > 4096)
    return Pixels();
  const int kMargin = 2;
  int width = bounds.right + kMargin, height = bounds.bottom + kMargin;
  Pixels pixels;
  {
    Canvas canvas(width, height);
    Point zero = { 0, 0 };
    newton_try
    {
      DrawShape(shapes, RA(NILREF), zero);
    }
    newton_catch_all
    {
    }
    end_try;
    pixels = canvas.take();
  }
  return Crop(pixels, std::max(0, bounds.left - kMargin), std::max(0, bounds.top - kMargin), width, height);
}

} // namespace ncx
