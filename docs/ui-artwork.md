# Interface artwork

The supplied `born2flap-ui-elements.png` and `born2flap-mockups.png` are preserved
in `Art/UI/Reference`. `Art/UI/Elements` contains 43 individually named PNGs and
a manifest of source rectangles and clipping shapes. Buttons, panels and badges
use transparent corners outside their frames. Painted interiors are preserved.
The ornamental brushstroke rings remain in the original reference sheet; they
are not used as rectangular UI controls.

Rebuild the cuts with `Unreal/Born2Flap/Tools/extract_ui_elements.ps1`, then run
`import_ui_elements.py` with Unreal's Python commandlet. Imported assets live
under `/Game/UI/Elements`, use uncompressed UI textures, no mipmaps and no
streaming. These assets are covered by the project's `bCookAll` setting.

The shared UMG renderer uses nine-slice frames for panels, buttons, tabs, fields
and headers, plus separate slider tracks/thumbs, toggle indicators and weather
badges. Button foregrounds change with the hover artwork; selection, disabled
states and existing action relays remain functional. Main menus have bounded
widths, and the one-world-at-a-time browser includes a landscape illustration.
These cards are concept artwork from the sheet, not screenshots of the levels.

Image generation: the built-in imagegen tool was tried with the prompt
"Remove only the blurred backdrop; preserve exact positions, dimensions, colors
and artwork; keep opaque panel/button interiors; make empty areas transparent."
That result retained the backdrop and was rejected. Shipped assets use exact
cuts from the user's original, with geometric clipping, rather than that output.

Validation: editor build, weather-menu selection/travel test, rendered menu and
settings captures. No new game modes or worlds from the mockup are implied.
