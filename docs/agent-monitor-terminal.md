<p align="right"><a href="agent-monitor-terminal.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Agent Monitor Terminal

This application replaces the hardware-demo entry point with a read-only, three-button terminal for FoloToy AI Passport (ESP32-C3, 8 MB Flash, no PSRAM). The original demo sources remain BSP regression references but are not compiled into this application.

## Screens and controls

Version 0.2.3 adds a permanent Tibo announcement destination on Overview,
independent of account quotas. UP/DOWN select it and OK opens the retained
announcement; hold OK returns to the same Overview item. Existing quota
announcement paging remains available. Focus surfaces use a single subtle edge
highlight and an eight-pixel radius within the original 24 KiB LVGL pool.
No realtime blur, gradient cache or continuous animation is allocated.

The Overview opens with running, needs-action and today's completed root-task counts. These remain the host's global counts, separate from the bounded recent-task list. The header identifies the latest snapshot's USB/BLE transport and delivery age. “Sync normal/timed out” describes snapshot freshness, not a physical connection claim. Battery remains top-right, including a valid zero reading.

UP/DOWN select Recent tasks, Recent alerts, Account quotas or Settings; OK enters. Each task row separates its title, client/status and project. A left focus marker and position counter identify the selection across three-row pages. Hold OK returns to the previous page and preserves its focus. A reordered snapshot keeps the selected task, alert or account by its exact ID; if an open task leaves the eight-task projection, details return to the task list.

Task details have basic/token pages, switched with UP/DOWN or OK. Basic details retain the title, project, round, child count, wall-clock duration and their own source age. All six token categories have separate rows; input/cache and output/reasoning inclusion remain explicit. Quota UP/DOWN selects another account window; OK switches between usage/window/reset and source/detail pages. A percentage is only graphed when supplied, including zero. Whole-day/hour window formatting uses the supplied minute count.

Alert OK confirms read; “read, pending sync” stays visible until the host receipt. Settings likewise distinguish a pending mute change. New events briefly display their title and state. OK confirms the event; other keys dismiss the transient view while retaining unread state. Long text ends with an ellipsis within fixed bounds.

Settings provide mute, 25/70/100% brightness and 20/60 seconds or disabled backlight timeout. Quiet hours arrive from the desktop, defaulting to local 22:00-08:00; equal endpoints disable the quiet interval. The first press after screen-off only wakes. Screen-off never enters deep sleep or disconnects the transports. Audio uses one short two-note cue only for a new event; historical snapshots, duplicates, quiet hours and manual mute are silent.

## Protocol and resource boundaries

USB Serial/JTAG and encrypted BLE Nordic UART share newline-delimited v1 JSON. Maximum line size is 8192 bytes. Oversized lines are dropped through their next newline, preserving later messages. Projection limits are eight tasks, six account windows and twelve alerts. UTF-8 copies end at code-point boundaries; absent numbers remain absent, including quota values, while zero is preserved.

Host hello returns device hello; every accepted snapshot returns `received` with its sequence. Both hello paths obtain firmware version 0.2.3 from the IDF application descriptor, matching the CMake project version. Twenty seconds without a snapshot marks offline, retaining old values and their age without changing task terminal states. BLE advertises `AgentPassport-XXXX`, supports both write modes, and uses bonded Secure Connections with a six-digit display passkey entered on the Mac. No Agent approval, cancellation or execution command exists.

Read acknowledgements and locally changed mute settings remain in device NVS until the host confirms them with `acknowledged` or `mute_applied`. Reconnect snapshots do not undo those pending user actions; retries are idempotent and do not replay event sounds.

If a snapshot already marks an event read, or the device has a pending acknowledgement for it, a later queued copy of that event cannot restore unread status, wake the screen or play sound. A new event whose current snapshot entry is still unread retains its normal notification behavior.

Task token detail and quota pages show their own source-observation age where supplied; a recently delivered snapshot is not substituted for a missing source timestamp. Empty project names remain empty, and zero token/percentage values remain visible.

Application state/JSON work runs outside button, LVGL and radio callbacks. UI access holds the BSP lock; audio has its own worker and uses the existing ES8311/shared-I2C owner. The single 20-line DMA display buffer and 24 KiB LVGL pool remain unchanged. The app does not start Wi-Fi.

## Chinese font

`assets/fonts/am_font_16.c` is Flash-resident, 16 px, 2-bit, uncompressed Noto Sans CJK SC Regular. The SIL OFL is retained beside it. It includes ASCII, the source font's U+4E00-U+9FFF basic ideographs and the common punctuation in the conversion command. It is not an arbitrary emoji or supplementary-ideograph font; missing-glyph placeholders stay enabled. Large glyph descriptors are enabled because the bitmap exceeds the small format's 1 MiB index range.

With lv_font_conv 1.5.3 and the official Noto source file:

```sh
lv_font_conv --font NotoSansCJKsc-Regular.otf \
  --range 0x20-0x7E,0x4E00-0x9FFF \
  --symbols '·，。！？：；（）【】《》、…→↑↓“”‘’—–～％￥' \
  --size 16 --bpp 2 --format lvgl --no-compress \
  --lv-font-name am_font_16 --lv-include lvgl.h \
  --output assets/fonts/am_font_16.c
```

## Verification

The 0.2.3 renderer adds direct announcement, current announcement and focused
Overview coverage to the existing scenarios: 34 frames plus 200 page changes.
Version 0.2.3 is built and archived separately; physical testing remains a separate
acceptance step. Prior-version results do not validate it.

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. The gate retains `build/FoloToy-AI-Passport-full.bin` for address `0x0` and a matching content-addressed debug archive. A merged flash replaces the retired application and may reset settings; no full-chip erase or original-firmware readback is required.

Protocol/model tests cover fragmented UTF-8, overflow recovery, null versus zero, quiet-hour boundaries, hello/receipt, silent history, duplicate events, acknowledgements, three-key navigation and ID-based focus through reorder/removal/empty snapshots. cJSON is the version pinned by ESP-IDF; its vendor-only macOS deprecation warnings are isolated from strict application warnings. The shared gate selects the native linker garbage-collection flag on macOS and verifies actionlint with Python's SHA-256 API.

Actual application UI can be host-rendered after dependencies are fetched:

```sh
cmake -S tests/ui -B /tmp/ai-passport-ui-build -G Ninja
cmake --build /tmp/ai-passport-ui-build
mkdir -p build/host-ui-0.2.3
/tmp/ai-passport-ui-build/am_ui_render tests/fixtures/am_ui_snapshot.json build/host-ui-0.2.3
```

These 31 240x320 PPM files use real UI/font code, synthetic fixture values and BSP corner geometry. The renderer checks every displayed glyph against its actual selected font, all object bounds, non-overlapping label bounds, a known-missing emoji negative case and 200 further page transitions within the original 24 KiB LVGL pool. It includes three clients, pending receipts, null/zero tokens and quotas, empty project/list states, long Chinese, offline, BLE-origin snapshots and pairing. Host memory measurements do not establish the device's free heap or largest free block.

Version 0.2.3 still needs separate physical boot/display/Chinese/buttons/audio/battery checks, real USB acknowledgement/reconnect, BLE pairing/fragments/reconnect/soak, and runtime-memory results. Prior-version physical results do not validate this image.

## Reset announcements

OK on quota pages cycles through remaining quota, source/absolute reset time,
and Tibo announcements. The announcement page is accessible even without
quota windows. Remaining percent is calculated only from source used percent;
missing values remain unknown. Reset timestamps explicitly use UTC+8 and
announcements never infer account state.

The optional resetNews projection retains state, a short title, original timing
wording, publication time and check time. Ambiguous wording never becomes a
precise deadline. These announcements concern Codex, not BigModel. New
reset_news events follow the existing unread, acknowledgement, mute and dedupe
rules without changing task counts. Host rendering adds current and stale
announcement screens, covering three-page navigation, glyphs, bounds and
duplicate events. Flashing and physical acceptance remain separate from host
rendering.
