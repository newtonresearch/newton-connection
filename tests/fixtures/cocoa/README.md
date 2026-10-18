# What the Cocoa NCX exported (PORTING.md 6.6)

Matt's Einstein (MessagePad 2000 setup, no personal data), backed up by
ncx-cli on 2026-10-18, then opened in the Cocoa Newton Connection 3.0.2,
which exported these. `translators_test golden` exports the same entries
with NCX and compares what the files hold, not their bytes.

- `einstein.newtondevice`: the backup, trimmed to the entries exported
  (Names, Calendar, To Do List, Calls, Notes, NewtWorks, the package
  hello:SIG; the other entries deleted with sqlite3, then VACUUM).
- `names-person.vcf`, `names-company.vcf`, `names-owner.vcf`: the Names
  export ("Newton Names..vcf", "-1..vcf", "-2..vcf": one file per class,
  the double dot the Cocoa app's).
- `dates.ics`: the Dates export ("Newton Dates..ics").
- `calls.text`: the Calls export, printed as NewtonScript (one of the two
  calls).
- `hello-SIG.newtonpkg`: the package.
- `works-paper.rtf`: "Untitled Paper 1.rtf".
- `works-drawing.png`: "Untitled Drawing.tiff" (814 x 382 at 144 dpi) as a
  PNG at 72 dpi: `sips -s format png`, `sips -z 191 407`.
- `ink-note.rtf`, `ink-note-graphics.png`: "11. Oct 2026.rtfd", a flat
  (serialised) RTFD file: its TXT.rtf, and its image.tiff (622 x 600 at
  144 dpi) as a PNG at 72 dpi (`sips -z 300 311`).

What the Cocoa app got wrong, and NCX does differently:
- Names: companies have no ORG and no FN (empty cards); a file per class.
- Dates: no UID; a line end in a title written as `\r`; one event twice.
- Works: Courier and Schoolbook become Helvetica Neue; underline lost; the
  drawing's bitmaps not drawn.
- Notes: ink words as "-ink-"; paragraphs in the order of the note's data,
  not top to bottom; the graphics at the end.
