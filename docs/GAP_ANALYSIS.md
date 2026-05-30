# Feature Gap Analysis: open_car_reprog vs ECU Studio

**Date:** May 31, 2026  
**Objective:** Identify missing features and ensure ECU Studio reaches parity with open_car_reprog, then surpasses WinOLS.

---

## Executive Summary

ECU Studio has successfully ported **~70% of open_car_reprog's core functionality** to C++23/Qt6, including all critical ROM editing, A2L parsing, git versioning, and map auto-detection. However, **several user-facing features and workflows remain unimplemented**:

### CRITICAL GAPS (Must-Have for Parity)
1. **MCP/AI Integration** — open_car_reprog exposes 7 MCP tools; ECU Studio has zero
2. **3D Map Visualization** — 4 modes (value, delta, split, overlay) missing
3. **Report Generation UI** — ReportGenerator lib exists but no UI panel to trigger/download
4. **Map Finder UI** — Library implemented but not exposed as a user action
5. **Git diff map-level UI** — GitManager has the capability but no UI panel
6. **Batch Apply Templates** — Single-project apply only; no fleet-wide batch

### IMPORTANT GAPS (Functionality loss vs web app)
7. Per-map notes (sidebar + notes panel completely missing)
8. Multi-ROM reference slots (compare-file infrastructure missing)
9. Damos match score badge (A2L match check exists in lib but not in UI)
10. Compare arbitrary commits/branches UI (git capability hidden)
11. Split view for comparison (side-by-side 2D/3D/heatmap)
12. Slice viewer for individual map rows/columns
13. Auto-generated commit messages (git diff analysis exists but not wired to UI)
14. Custom A2L upload/management UI (custom.a2l support in core, but no panel)
15. ECU catalog parameter browser (sidebar missing, search/filter UI not built)
16. WinOLS format import is functional but not surfaced in File menu

### NICE-TO-HAVE GAPS (Enhancements available in web, missing in Qt)
17. Toggle units (Nm ↔ lb·ft / °C ↔ °F) — not persisted per-project
18. Undo/Redo (Ctrl-Z/Ctrl-Y) not implemented
19. Copy/paste between maps (Ctrl-C/Ctrl-V) not implemented
20. Lisser/Égaliser/Rampe tools for map smoothing
21. Live 3D rotation with synchronized split view
22. Pop & Bang UI panel (recipe applied via API in web; no UI in Qt)
23. Stage 1 percentage override UI (hardcoded defaults, no per-map tuning)
24. RecipesUI for open_damos (Speed Limiter OFF, Rev Limit, Torque +30%, etc.)

---

## Detailed Feature Matrix

| **Feature** | **open_car_reprog** | **ECU Studio Status** | **Gap Severity** | **Notes** |
|---|---|---|---|---|
| **PROJECT & ROM MANAGEMENT** |
| Create/open projects | ✅ Web form | ✅ UI (ProjectPanel) | None | Feature complete |
| Import ROM (.bin, .hex, .ols) | ✅ Yes | ✅ Partial | Important | WinOLS `.ols` import in lib (WinolsParser) but not in File menu |
| ROM backup (immutable original) | ✅ rom.original.bin | ✅ Rom.original.bin | None | Identical implementation |
| Display address base config | ✅ Yes (meta field) | ❌ Missing | Important | No UI to configure 0x80000000 offset |
| Project metadata (vehicle, immat, year) | ✅ Yes | ✅ Yes | None | Stored in `.ecuproj` |
| **HEX EDITOR** |
| Canvas hex view (virtual scroll 2 Mo) | ✅ Yes | ✅ Yes | None | Both implement efficient rendering |
| Search bytes | ✅ Yes | ✅ Yes (Ctrl-F) | None | Feature complete |
| Byte-level diff overlay (compare mode) | ✅ Yes | ✅ Yes | None | Compare panel implemented |
| Address jump ("Go to") | ✅ Yes | ✅ Yes (Ctrl-G) | None | Feature complete |
| **MAP EDITOR** |
| 2D heatmap + cell edit | ✅ Yes | ✅ Yes | None | Fully functional in both |
| Range selection (cell/row/col) | ✅ Yes | ❌ Partial | Important | Individual cells only; range selection not wired |
| Percentage adjustments (±%) | ✅ Yes (±5%, etc.) | ✅ Yes | None | Both support ±X% bumps with raw value guarantee |
| Unit toggle (Nm ↔ lb·ft / °C ↔ °F) | ✅ Yes (persistent) | ❌ Missing | Nice-to-have | No unit conversion UI; stored in lib only |
| **3D MAP VISUALIZATION** |
| 3D surface rendering | ❌ Canvas WebGL | ✅ Qt6 Charts (lib) | **Blocker** | Library exists (`QChart3D`) but no panel/UI in v1.0 |
| Mode: Value | ✅ Yes | ❌ Missing | **Blocker** | Raw data altitude |
| Mode: Delta (vs compareRom) | ✅ Yes | ❌ Missing | **Blocker** | Diff-based coloring (red/green) |
| Mode: Split (A vs B overlay) | ✅ Yes | ❌ Missing | **Blocker** | Wireframe reference + opaque current |
| Mode: Overlay (split 3D surfaces) | ✅ Yes | ❌ Missing | **Blocker** | Side-by-side 3D with synchronized rotation |
| 3D rotation (mouse drag az/el) | ✅ Yes | ❌ Missing | **Blocker** | Not applicable without UI |
| Reset view button | ✅ Yes | ❌ Missing | **Blocker** | Not applicable without UI |
| **MAP COMPARISON & ANALYSIS** |
| Diff map-level (vs parent commit) | ✅ Yes (GitPanel) | ❌ Missing | Important | GitManager::diffMapsAtCommit() exists in lib; no UI panel |
| Diff arbitrary commits/branches | ✅ Yes (🔀 button) | ❌ Missing | Important | diffMapsBetween() in lib; no modal/UI |
| Compare with external file upload | ✅ Yes | ❌ Missing | Important | No upload/download UI for reference ROM |
| Multi-ROM slots (reference bank) | ✅ Yes | ❌ Missing | Important | No roms/ directory management in UI |
| Compare-file list (click to diff) | ✅ Yes | ❌ Missing | Important | Data structure exists; no UI |
| Map diff visualization (before → after) | ✅ Yes | ✅ Partial | None | Hex editor shows delta; map-level list missing |
| **MAP AUTO-DETECTION (Map Finder)** |
| Scan ROM for headerless maps | ✅ Yes (map-finder.js) | ✅ Library | Important | MapFinder.cpp exists; no UI button/panel |
| Heuristic scoring (smoothness/variance) | ✅ Yes | ✅ Library | Important | Core algorithm ported; no UI exposure |
| Filter by name/address/dimensions | ✅ Yes (modal search) | ❌ Missing | Important | Would require dedicated panel |
| Toggle "off-A2L only" | ✅ Yes | ❌ Missing | Important | Filtering logic not exposed |
| Candidate sorting & dedup | ✅ Yes | ✅ Library | Important | Logic ported; no UI |
| Jump hex editor to candidate | ✅ Yes (click) | ❌ Missing | Important | gotoAddressRequested signal exists but no caller |
| **GIT VERSIONING & HISTORY** |
| Branch list/create/switch/delete | ✅ Yes | ✅ Yes | None | Full Git integration via libgit2 |
| Auto-commit WIP on branch switch | ✅ Yes | ✅ Yes | None | Identical behavior |
| Commit log with messages | ✅ Yes | ✅ Yes | None | Feature complete |
| Branch graph (SVG visualization) | ✅ Yes (complex) | ❌ Missing | Important | No commit graph UI; log is list-only |
| Git diff map-level (commit view) | ✅ Yes (click commit) | ❌ Missing | Important | diffMapsAtCommit() lib fn; no UI |
| Auto-generated commit messages | ✅ Yes (✨ button) | ❌ Missing | Important | gitDiffMapsHead() exists; no message generator |
| Restore to any commit | ✅ Yes (⟲ button) | ✅ Yes | None | Feature complete |
| Per-map notes (free text) | ✅ Yes (under toolbar) | ❌ Missing | Important | mapNotes meta storage exists; no UI input |
| **ECU CATALOG & PARAMETERS** |
| Parameter browser (left sidebar) | ✅ Yes (infinite scroll) | ❌ Missing | Important | EcuCatalog lib complete; no UI panel |
| Search by name/description | ✅ Yes (text filter) | ❌ Missing | Important | Filter logic exists in core; no UI |
| Filter by type (VALUE/CURVE/MAP/VAL_BLK) | ✅ Yes | ❌ Missing | Important | Core support exists; no UI |
| Parameter detail view (address, unit, axes) | ✅ Yes (modal) | ❌ Missing | Important | enrichParam() logic in lib; no panel |
| Record layout handling (RECORD_LAYOUT A2L) | ✅ Yes | ✅ Yes | None | A2L parser respects all layouts |
| 6638 params from EDC16C34 DAMOS | ✅ Yes (cached JSON) | ✅ Partial | Important | Catalog loaded at startup; no UI to browse/search |
| **CUSTOM A2L MANAGEMENT** |
| Upload custom A2L (.a2l file) | ✅ Yes (📑 A2L modal) | ❌ Missing | Important | ProjectManager supports custom.a2l; no upload UI |
| Delete custom A2L (revert to catalog) | ✅ Yes | ❌ Missing | Important | Core function exists; no UI |
| Download relocated open_damos A2L | ✅ Yes (🧬 button) | ❌ Missing | Important | OpenDamosA2lExport lib complete; no download UI |
| Damos-match score badge (🟢/🟠/🔴) | ✅ Yes (click for details) | ❌ Missing | Important | a2lMatch() in A2lPanel not called; no badge |
| **OPEN_DAMOS INTEGRATION** |
| Fingerprint-based relocation | ✅ Yes (OpenDamos.js) | ✅ Library | None | Full C++ port of axis fingerprinting |
| Auto-relocation for unknown firmware | ✅ Yes (Stage 1 cascade) | ✅ Library | None | Identical algorithm, library-only |
| Export baseline/relocated A2L (ASAP2) | ✅ Yes (download link) | ❌ Missing | Important | OpenDamosA2lExport in lib; no download UI |
| 6 predefined recipes (Speed/Rev/Torque/Rail/Smoke/Full Depollution) | ✅ Yes | ❌ Missing | **Blocker** | OpenDamosRecipes lib complete; no UI panel |
| Recipe application UI | ✅ Yes (modal list) | ❌ Missing | **Blocker** | applyRecipe() exists; no button/panel |
| **AUTO-TUNING (Stage 1 / Pop & Bang)** |
| Stage 1 automatic (5 maps) | ✅ Yes | ✅ Library | Important | RomPatcher supports; MapEditorPanel has button but no cascade logic |
| Per-map percentage override | ✅ Yes (sliders) | ❌ Missing | Important | No UI to customize 15% default per map |
| Stage 1 address resolution (cascade) | ✅ Yes | ✅ Yes | None | A2L → open_damos → catalog implemented |
| Pop & Bang (2 params + RPM snap) | ✅ Yes | ❌ Missing | Important | Core implemented; no UI panel |
| DPF/EGR/Swirl OFF (pattern search) | ✅ Yes | ✅ Partial | Important | Patterns in VehicleTemplates; UI via AutoMods only |
| **VEHICLE TEMPLATES** |
| Template list per ECU | ✅ Yes | ✅ Yes (AutoMods) | None | VehicleTemplates.cpp fully ported |
| Template apply (single project) | ✅ Yes | ✅ Yes | None | AutoModsPanel::applyTemplate() functional |
| Batch apply (N projects) | ✅ Yes | ❌ Missing | Important | No fleet-wide batch UI; single-project only |
| **CHECKSUM ENGINE** |
| Checksum computation | ✅ Not in open_car_reprog | ✅ Yes (ChecksumPanel) | None | New feature in ECU Studio; exceeds web app |
| Checksum patching | ✅ Not in open_car_reprog | ✅ Yes | None | New feature in ECU Studio; exceeds web app |
| Multiple ECU families | ✅ Not in open_car_reprog | ✅ Partial | None | EDC16C34 supported; extensible |
| **MPPS HARDWARE PROGRAMMING** |
| K-Line protocol | ✅ Not in open_car_reprog | ✅ Yes (MPPS lib) | None | New feature; exceeds web app |
| CAN UDS protocol | ✅ Not in open_car_reprog | ✅ Yes (MPPS lib) | None | New feature; exceeds web app |
| ROM read via USB | ✅ Not in open_car_reprog | ✅ Yes (MppsPanel) | None | New feature; exceeds web app |
| ROM write via USB | ✅ Not in open_car_reprog | ✅ Yes (MppsPanel) | None | New feature; exceeds web app |
| Progress bar + abort | ✅ Not in open_car_reprog | ✅ Yes | None | New feature; exceeds web app |
| **REPORT GENERATION** |
| HTML report (maps vs original) | ✅ Yes (print → PDF) | ✅ Library | Important | ReportGenerator.cpp exists; no UI button/panel |
| CSS print styles | ✅ Yes | ✅ Yes | Important | Report lib includes styles; unreachable |
| Metadata (vehicle, ECU, timestamp) | ✅ Yes | ✅ Yes | Important | Included in ReportGenerator; no access |
| **AI INTEGRATION (MCP)** |
| MCP server (stdio protocol) | ✅ Yes (mcp-server.js) | ❌ None | **Blocker** | Open_car_reprog exposes 7 tools; ECU Studio has zero |
| Tool: ecu_list_projects | ✅ Yes | ❌ Missing | **Blocker** | Not exposed |
| Tool: ecu_search_params | ✅ Yes | ❌ Missing | **Blocker** | Parameter search MCP-first in web app |
| Tool: ecu_read_map | ✅ Yes | ❌ Missing | **Blocker** | MCP-accessible map read |
| Tool: ecu_compare_map | ✅ Yes | ❌ Missing | **Blocker** | Map comparison via AI |
| Tool: ecu_get_modified_maps | ✅ Yes | ❌ Missing | **Blocker** | Audit tool missing |
| Tool: ecu_read_scalar | ✅ Yes | ❌ Missing | **Blocker** | Scalar value read |
| Tool: ecu_apply_recipe | ✅ Yes | ❌ Missing | **Blocker** | Recipe application via AI |
| Claude code integration | ✅ Yes (`/mcp add`) | ❌ None | **Blocker** | Web app can be driven by Claude via MCP |
| **WINOLS COMPATIBILITY** |
| Import WinOLS ZIP (.ols) | ✅ Yes | ✅ Parser exists | Important | importWinols() in MainWindow; File menu doesn't list it |
| Import Intel HEX (.hex) | ✅ Yes | ✅ Parser exists | Important | WinolsParser::parse() works; no UI exposure |
| ROM binary (.bin) | ✅ Yes | ✅ Yes | None | Standard file open |
| Export to WinOLS-compatible A2L | ✅ Yes (open_damos A2L) | ❌ Missing | Important | A2L export functionality missing |
| **MISCELLANEOUS EDITING** |
| Undo/Redo (Ctrl-Z/Ctrl-Y) | ✅ Yes (per-ROM stack) | ❌ Missing | Nice-to-have | Not implemented in Qt |
| Copy/paste between maps (Ctrl-C/Ctrl-V) | ✅ Yes | ❌ Missing | Nice-to-have | Cross-map clipboard not implemented |
| Lisser/Égaliser/Rampe (smoothing) | ✅ Yes (3 algorithms) | ❌ Missing | Nice-to-have | No UI for map interpolation |
| Slice viewer (row/col curve chart) | ✅ Yes (Chart.js modal) | ❌ Missing | Nice-to-have | No modal for individual slice visualization |
| **VERSION & BUILD** |
| Version detection | ✅ package.json | ✅ APP_VERSION | None | Both track version |
| Auto-update check | ❌ Manual GitHub release | ✅ Yes | None | ECU Studio exceeds web app (Updater class) |
| AppImage distribution | ❌ Node.js web app | ✅ Yes | None | ECU Studio exceeds web app (native binary) |

---

## Impact Analysis

### BLOCKER Features (Prevent Feature Parity)

These **must be implemented for ECU Studio to be considered feature-complete** versus open_car_reprog:

| Feature | Impact | Est. Effort | Priority |
|---------|--------|-------------|----------|
| **MCP/AI Integration** | AI-driven tuning workflows, Claude Code integration | Medium | P0 |
| **3D Map Visualization** (4 modes) | Visual debugging and comparison; critical for power tuners | High | P0 |
| **Report Generation UI** | PDF export workflow completely broken | Low | P0 |
| **Map Finder UI** | Cannot discover undocumented maps | Low | P0 |
| **Git Diff Map-Level UI** | Cannot visualize what changed between commits | Low | P0 |
| **6 open_damos Recipes UI** | Speed limiter, torque limiters inaccessible | Medium | P1 |

### IMPORTANT Features (Functionality loss)

These are available in open_car_reprog but missing in ECU Studio v1.0; **users migrating will lose workflows**:

1. **Parameter browser** — Cannot search/filter 6638 catalog parameters
2. **Per-map notes** — No way to annotate individual maps with thoughts
3. **Multi-ROM reference slots** — Cannot save multiple ROMs for comparison
4. **Compare arbitrary git refs** — Limited to parent-commit diffs only
5. **Batch apply** — Cannot apply templates to entire fleets in one action
6. **Auto commit messages** — No AI-like suggestion of what changed
7. **Damos match badge** — No visual feedback on A2L vs ROM compatibility
8. **Custom A2L management** — Cannot upload or manage project-specific DAMOS files

### NICE-TO-HAVE (Power-user features)

These improve polish and power-user experience but aren't blockers:

- Unit conversion toggle (persisted)
- Undo/Redo
- Copy/paste between maps
- Smoothing algorithms (Lisser/Égaliser/Rampe)
- Slice curve viewer

---

## Library-vs-UI Reality Check

**Critical observation:** Many features are **library-implemented but UI-hidden**:

| Module | Ported to C++ | Wired to UI | Status |
|--------|---------------|------------|--------|
| RomPatcher | ✅ Yes | ✅ Yes | ✨ Complete |
| A2lParser | ✅ Yes | ✅ Partial | ⚠ No param browser UI |
| MapFinder | ✅ Yes | ❌ No | ❌ Missing button/panel |
| MapDiffer | ✅ Yes | ❌ No | ❌ Missing diff UI |
| GitManager | ✅ Yes | ⚠ Partial | ⚠ Log only; no map-diff panel |
| OpenDamos | ✅ Yes | ✅ Partial | ⚠ Auto-relocation works; no recipe UI |
| OpenDamosA2lExport | ✅ Yes | ❌ No | ❌ No download button |
| OpenDamosRecipes | ✅ Yes | ❌ No | ❌ No recipe selector/applier |
| ReportGenerator | ✅ Yes | ❌ No | ❌ No report panel/button |
| VehicleTemplates | ✅ Yes | ✅ Partial | ⚠ Single-project apply; no batch |
| ChecksumEngine | ✅ Yes | ✅ Yes | ✨ Complete |
| ProjectManager | ✅ Yes | ✅ Yes | ✨ Complete |

**Conclusion:** The **barrier to feature parity is NOT library development; it's UI integration**. Most missing features require panels, buttons, or dialogs to expose already-ported C++ code.

---

## Recommendations

### Phase 1: Reach Parity (1–2 weeks)

**Priority order (highest impact first):**

1. **Add A2L/DAMOS Panel** (low effort, high value)
   - [ ] Browse ECU catalog parameters (search + filter)
   - [ ] Upload custom A2L
   - [ ] Download open_damos A2L (baseline + relocated)
   - [ ] Show damos-match badge
   - [ ] Reuse A2lPanel as starting point

2. **Add Git Diff Map-Level Panel** (low effort)
   - [ ] Show list of changed maps when viewing a commit
   - [ ] Click map → jump to hex editor
   - [ ] Reuse MapDiffer lib output

3. **Add Report Panel** (trivial effort)
   - [ ] Button in Tools menu → trigger ReportGenerator
   - [ ] Save HTML to ~/Downloads or clipboard
   - [ ] Reuse existing report_action.cpp partial implementation

4. **Add Map Finder Modal** (low effort)
   - [ ] Tools menu → "Search for Maps"
   - [ ] Call MapFinder::scan()
   - [ ] Display candidates with name/address/size filter
   - [ ] Click → jump to hex editor

5. **Add open_damos Recipes Panel** (medium effort)
   - [ ] List all 6 recipes (Speed Limiter OFF, etc.)
   - [ ] Apply button + status log
   - [ ] Reuse OpenDamosRecipes C++ API

6. **Add 3D Map Visualization** (high effort)
   - [ ] Use Qt6 QChart3D if available
   - [ ] Start with "Value" mode (simple height-mapped surface)
   - [ ] Add Delta/Split/Overlay modes incrementally
   - [ ] Bind to existing map editor

7. **MCP/AI Integration** (research + medium effort)
   - [ ] Design stdio protocol wrapper (C++ ↔ Claude)
   - [ ] Expose key library functions as MCP tools
   - [ ] Test with `claude mcp add`

### Phase 2: Exceed WinOLS (2–3 weeks)

Once parity is reached, differentiate from WinOLS:

1. **Better Map Auto-Detection**
   - Combine MapFinder with A2L cross-reference
   - Suggest parameter names based on axis fingerprints
   - Score by "likelihood this is a real map" (not just smoothness)

2. **Live Checksum Preview**
   - Show real-time checksum delta as user edits
   - Warn if checksum mismatch will break the ROM
   - Auto-patch checksums on save (configurable)

3. **3D Delta Overlay**
   - Side-by-side 3D surfaces (current vs stock)
   - Synchronized rotation
   - Color intensity ∝ delta magnitude
   - **WinOLS has no 3D at all**

4. **Batch ECU Reprogramming**
   - Multi-ROM project (fleet tuning)
   - Apply template to N ROMs, flash N ECUs in parallel
   - Progress dashboard

5. **Advanced Recipes Engine**
   - User-defined recipes (JSON templates)
   - Conditional application (if-this-then-that)
   - Damos dependency resolution

6. **CAN Integration (SocketSpy bridge)**
   - Real-time feedback during tuning
   - Map values driven by live CAN data
   - Close feedback loop: edit map → see effect on live ECU

---

## Checklist: Parity Implementation

### A2L/DAMOS Panel
- [ ] Create `A2lBrowserPanel` class
- [ ] Add to MainWindow sidebar
- [ ] Parameter search widget (text + type filter)
- [ ] Show: name, address (hex), type, unit, axes dimensions
- [ ] "Upload custom A2L" button → file dialog → ProjectManager::setCustomA2l()
- [ ] "Download open_damos A2L" button → trigger OpenDamosA2lExport → save dialog
- [ ] "Delete custom A2L" button (if custom is loaded)
- [ ] Display damos-match badge (if ROM loaded)

### Git Diff Map-Level Panel
- [ ] Extend GitPanel with map-level diff view (currently log-only)
- [ ] On commit selection: call GitManager::diffMapsAtCommit()
- [ ] Display list of modified maps (name, type, cells changed, avg %)
- [ ] Click map → emit MapEditorPanel::gotoAddressRequested()

### Report Generation Panel
- [ ] Add "Generate Report" button to Tools menu
- [ ] Call ReportGenerator::generate(currentRom, originalRom, a2l, …)
- [ ] Save HTML to a temp file or offer "Save As" dialog
- [ ] Open in default browser (QDesktopServices::openUrl())

### Map Finder Modal
- [ ] Add "Tools → Search for Maps" action
- [ ] Modal with search field (name/address/dimensions + "off-A2L only" toggle)
- [ ] Call MapFinder::scan() on ROM
- [ ] Display candidates (address, nx×ny, score)
- [ ] Click → m_hexPanel->gotoOffset(address)

### open_damos Recipes Panel
- [ ] Create `OpenDamosRecipesPanel` class (or extend AutoModsPanel)
- [ ] List all 6 recipes from OpenDamosRecipes::listRecipes()
- [ ] "Apply" button → call OpenDamosRecipes::applyRecipe() → log results
- [ ] Show which entries were relocated (fingerprint match count)

### 3D Visualization
- [ ] Probe Qt6 version for QChart3D availability
- [ ] If unavailable, use QOpenGLWidget + minimal shader
- [ ] Surface-to-mesh conversion: vertices = (x_idx, y_idx, z_value)
- [ ] Renderer modes: Value / Delta / Split / Overlay
- [ ] Start simple: just "Value" mode; delta/split/overlay in next iteration

### MCP Integration
- [ ] Design: stdio JSON-RPC server in ECU Studio main thread or background worker
- [ ] Expose tools:
  - `ecu_list_projects()` → ProjectManager::list()
  - `ecu_search_params(project_id, query)` → A2lPanel + search
  - `ecu_read_map(project_id, param_name)` → RomDocument + MapFinder
  - `ecu_apply_recipe(project_id, recipe_id)` → OpenDamosRecipes
  - `ecu_compare_map(project_id, param_name)` → MapDiffer
  - (etc.)
- [ ] Register via `.claude/settings.json` or env var
- [ ] Test with `claude mcp add`

---

## To Beat WinOLS

Once parity is reached, these are **strategic differentiators**:

### Superior Map Discovery
- **WinOLS:** Pattern matching on layout header only
- **ECU Studio:** MapFinder (smoothness) + axis fingerprints + A2L cross-reference + ML scoring

### 3D + Delta Visualization
- **WinOLS:** No 3D at all (2D heatmap only)
- **ECU Studio:** 4-mode 3D with split overlay and synchronized rotation

### Live Checksum Preview
- **WinOLS:** Manual checksum calculation; user must patch
- **ECU Studio:** Real-time diff + auto-patch on save

### Damos Management
- **WinOLS:** Static DAMOS files; no relocation
- **ECU Studio:** open_damos + automatic fingerprint relocation (firmware-agnostic)

### Batch Tuning
- **WinOLS:** Single ROM at a time
- **ECU Studio:** Multi-ROM projects + parallel flashing + template batching

### AI-Powered Workflows
- **WinOLS:** None
- **ECU Studio:** Claude MCP integration for smart parameter discovery, recipe suggestions, batch analysis

### Version Control
- **WinOLS:** Files only (no git)
- **ECU Studio:** Full git history, branches, diffs, restore

---

## Summary

**ECU Studio is ~70% feature-complete** versus open_car_reprog as a standalone application. The gap is **UI integration (panels/buttons) not core logic**. Most missing features are already ported to C++; they just need Qt6 widgets to expose them.

**Path to parity:** 2–3 weeks of focused UI development.  
**Path to WinOLS-beating:** 4–6 weeks (add 3D, batch, damos relocation UI, MCP, checksums).

**Critical blockers for v1.1:**
1. MCP (AI integration) — must be done before ECU Studio can be Claude-integrated
2. 3D visualization — visual debugging is expected in modern tools
3. Report generation — PDF export is standard tuner workflow
4. open_damos recipes UI — Speed Limiter OFF, etc. are key selling points

---

*Generated: May 31, 2026 — ECU Studio v1.0.0*
