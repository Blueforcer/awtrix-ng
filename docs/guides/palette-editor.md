# Palette editor

A palette is a set of colours AWTRIX ramps through. Effects paint out of it, and so can text,
charts and the progress bar - see [Effects & overlays](effects.md#recolour-an-effect-with-a-palette)
and [Text & colors](text.md#painting-from-a-palette).

The web UI's **Palettes** tab is where you make them: a list of every palette AWTRIX knows on the
left, one editor on the right.

## A palette is 1 to 16 colour stops

You give the colours you care about - the **stops** - and everything between them is filled in.
Two stops are a ramp from one colour to the other. Sixteen stops are each colour exactly as you set
it.

So a red-to-blue gradient is **two** stops, not sixteen shades you mix by hand.

The ramp under the name in every list row, and the big one in the editor, is drawn the same way
AWTRIX draws it. What you see there is what the matrix shows.

## Make one

1. Open the **Palettes** tab and press **New palette**.
2. The ramp itself is the editor. Every stop is a handle sitting on it:
    - **click the ramp** to add a stop there, in the colour that is already at that point, or press
      **Add stop** to drop one into the widest gap
    - **drag a handle** to move its colour along the ramp - with a handle selected, the left and
      right arrow keys nudge it by one percent, or by ten with Shift held
    - the row below edits whichever handle is selected: a colour picker with the hex written across
      it, the position field marked **at**, and a **✕** button that removes the stop
3. Press **Try on AWTRIX** at any point to throw it on the matrix for four seconds - it works before
   you have saved anything.
4. Type a name and **Save**.

New stops are spread evenly until you move one. From then on each stop keeps the position you gave
it, and **Spread evenly** hands that back.

Positions decide how much of the ramp each colour gets: `Heat` spends most of its length in the reds
because its bright colours sit near the end. Two stops at the *same* position are a hard edge with no
blend between them, which is how you build stripes.

Names may use letters, digits, `-` and `_`, up to 24 characters. The palette is now usable anywhere a
palette name goes, for example `{"effect":"Plasma","palette":"sunset"}`.

## Change one you have

Pick it in the list, edit, **Save**. Give it a different name before saving and the button turns into
**Save as new**, which keeps the original and stores a copy.

The **Delete** button on a row - the bin - removes that palette; it asks once, so press it a second
time to confirm. Apps already showing that palette keep rendering with the colours they have; it is
the *next* push or update naming it that fails with `422 validationFailed`. Point those apps at a
palette that still exists.

## Change a built-in

Pick one of the eight [built-in palettes](../reference/visuals.md#palettes), change the colours, and
press **Replace built-in**: from then on everything that asks for `Heat` gets your version, and the
row is marked **edited**.

**Restore the built-in** - the same bin button, on a row marked **edited** - puts the original back.
You cannot lose a built-in this way; it is always one press from returning.

Prefer the **Duplicate** button beside it when you want your own palette *based on* a built-in rather
than in place of it: it copies the colours under a free name for you to rename and save.

## Good to know

- **Blend**, above the ramp, is a preview switch only. It shows the difference between a smooth ramp
  and the 16 hard bands you get with `"paletteBlend": false`; each app sets that for itself.
- Palettes are plain text files under `/PALETTES` on AWTRIX, and they travel with a
  [backup](../reference/http.md#backup-restore). You can write one by hand and upload it instead -
  see [custom palettes](../reference/visuals.md#custom-palettes) for the line format. A file named
  after a built-in is what replaces it, which is why deleting the file brings the built-in back.
- Editing a palette does not change apps that are already using it. They keep the colours they had
  when they were created or last updated. Push the app again - same palette name is fine - to pick
  up the new colours.
- A few effects draw fixed colours and never look at a palette. The
  [effects table](../reference/visuals.md#background-effects) marks which.

## Related

- [Effects & overlays](effects.md) - the effects a palette recolours
- [Text & colors](text.md#painting-from-a-palette) - painting text and charts out of a palette
- [Visuals reference](../reference/visuals.md#palettes) - the built-in palettes and the file format
