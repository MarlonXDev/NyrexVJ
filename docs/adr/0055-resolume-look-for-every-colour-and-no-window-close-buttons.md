# ADR 0055: One Resolume look for every colour, stepper fields on the sliders, deck tabs and no close buttons on the tool windows

- **Status:** Accepted, requested by MarlonXDev after the 0.60.0 look (2026-11-27 01:05): the X at the top of the windows is broken and should go, the interface should be "1 to 1" like Resolume Arena, it should keep that look when he changes the colour, and the slider backgrounds and the rest should be copied as closely as possible. Issue [#140](https://github.com/MarlonXDev/NyrexVJ/issues/140). **Checked on a virtual screen with software OpenGL only; the look was compared with four Resolume screenshots by measuring their colours, not on a real screen next to Resolume.**
- **Date:** 2026-11-27
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0054](0054-resoport-glass-effects-transitions-and-minimized-output.md), [ADR 0053](0053-window-layout-transport-bar-and-docking-timeline.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md)

## Context
- Version 0.60.0 gave the Resolume look to the theme Arena only. The ten other themes kept the older look (rounded corners, slim round sliders), so choosing Blue or Red took the new look away.
- The floating tool windows (Settings, Creator, Manual, Shader editor, Timeline and others) were opened with a close flag, and Dear ImGui drew its close button in the title bar. With the Arena colours it showed as a grey square without a cross.
- Four Resolume Arena screenshots (7.3.3) were sent. Measured in them: the rows of a parameter panel are `#141412`, the lines between the rows are almost black, the empty part of a slider is `#080808`, the filled part is `#252525`, the marker is 3 pixels wide in the accent, the bar sits about two pixels inside the row, the value is written left of two small fields for minus and plus, the tab bars are `#2A2A2A` with the selected tab in the darkest colour `#0A0909`, and the decks are named tabs with the selected one in mint.

## Decision
1. **The look belongs to the interface, the colour only sets the accent.** `applyTheme` has one set of neutrals and shapes (flat corners, flat bars, grey bands) for all eleven themes. The accent comes from the colour of the theme in HSV: the marker and check marks use hue with saturation 0.45 and value 0.55 (raised in steps until the contrast to the field colour is 3.3 at least), the highlight for the selected column, layer and deck uses saturation 0.50 and value 1.0, the dark accent uses value 0.32. For Arena this gives the values of 0.60.0. Mono has no hue and stays grey. High contrast keeps its own palette and the outlines (`ui::kFlatLook` is off there), as before.
2. **Sliders.** Bar inset by two pixels, filled part `#252525` (`#303030` when hovered or dragged), empty part `#080808`, line under each row, the value left of two stepper fields. The fields step by one percent of the range (integers by one, logarithmic sliders by five percent), repeat while held, and are left out when the row is narrow (less than 72 points of bar) and in the layer block of the grid. Ctrl+click for typing is unchanged.
3. **Tabs and title bars.** Dock tab bars and title bars use `#2A2A2A`, the unselected tab the same colour, the selected tab `#0A0909`. The small menu arrow of the windows (`WindowMenuButtonPosition`) is off.
4. **Deck tabs.** The deck row shows the used decks by name (and the active one), the selected deck in the highlight with dark text, and a plus button that starts the next free deck. The numbered list stays for narrow windows. The context menu of a deck (rename, copy, fill, clear, remove) is unchanged.
5. **No close buttons on tool windows.** `ui::beginTool` opens the window without a close flag, so there is no button in the title bar. **Escape closes the focused tool window** when no field is being edited and no popup is open (Settings had this already). The windows that have a menu entry with a check mark can still be closed there; the shader editor still asks about unsaved changes before it closes.

## Not done
- Resolume's own font (the system sans font stays), icons, the Dashboard knobs, the M, A and V buttons and the T and Alph buttons of the layers, the crossfader row, a third layer row, the text inside the slider bars, the audio and effect rows with the coloured headers (no counterparts or no function behind them), and the old rounded look as a choice. Whoever wants it back can say so; it would be a second look in `applyTheme`.
- No change of what the windows do.

## Consequences
- Every colour looks the same, so the screenshots in the manual are taken once with Arena.
- A user of the old Studio look sees the new shapes with the Studio colour.
- Tool windows have no mouse way to close except the menu entry; Escape is the main one. This is stated in the manual.
- Not tried on a real screen, with Windows scaling, or with the Resolume font; see `M-UI-9`.
