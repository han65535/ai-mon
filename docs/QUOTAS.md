# Remaining allowance and tray graphs

AI Mon 0.3.4 prioritizes the **last-reported subscription allowance** over local token totals.

## What the tray shows

There are two tray icons: brown for Claude, blue for Codex. Each number is the lower remaining percentage among the windows actually reported for that provider, rounded down to a whole percent. Hover to identify the provider and see each window's percentage. The left miniature bar is the short window; the right is weekly. Missing windows stay gray and do not become zero or 100%.

Click either icon to open the detailed view. Each provider has a remaining-percentage bar and recorded-history graph for its short window and weekly window, plus the reported reset countdown. The graph spans the reported window, plots only observed samples, and never connects data from different reset windows or plans. Newly connected providers initially have only a point, not a fabricated history.

Green means more than 25% remains, amber means 10–25%, and red means 10% or less. These are display thresholds, not provider policies. A value reported more than 15 minutes ago is marked as a last-known value. When the reported reset time passes, the percentage becomes unknown until another report arrives; the app does not assume a refill.

## Mini graphs (0.3.4)

Choose **Mini graphs** in the main window or either tray icon's context menu. A borderless strip shows Claude and Codex on two rows, each with short-term and weekly remaining percentages and solid bars. It targets twice the clock area's width and the same height, clamped to 176–260 × 40–56 logical pixels for legibility. If the shell does not expose the clock rectangle, it uses 200 × 48. The compact percentages round down to whole numbers; hover for the detailed values and stale-data status. Drag anywhere to move it. Right-click for settings, **Always on top**, **Move next to taskbar**, refresh, details or hide. It stays independent when the detailed window is hidden or minimized.

Version 0.3.4 uses dark navy panels, bright text, cyan short-term bars and lime weekly bars. The remaining bars are 6–8 logical pixels thick instead of a one-pixel line. Amber/red still indicate low remaining allowance; missing values show a dash and an unfilled track. Windows high-contrast colors take precedence. Time-series history is shown in the detailed window, where it has space to be readable.

Visibility, screen position and the always-on-top preference are stored in `mini-window.json` in the app data folder. On restart or a monitor/work-area change, its position is clamped to an available monitor's work area. Moving next to the taskbar places it just inside the usable desktop edge, including top/left/right taskbar layouts. The mini window has its own DPI-scaled fonts and follows language changes.

This is a standalone native window beside the taskbar. Legacy [DeskBands](https://learn.microsoft.com/en-us/windows/win32/shell/band-objects) can be hosted in the old Windows taskbar, but Microsoft documents that Windows 11 [does not let apps customize taskbar areas](https://www.microsoft.com/en-us/windows/windows-11-specifications). AI Mon keeps the compatible tray icons and a separate mini window. It does not install an Explorer extension.

The mini window reuses the same snapshots as the detailed view and adds no collection process or network polling. English and Korean installers and the portable executable are available from the [0.3.4 release](https://github.com/han65535/ai-mon/releases/tag/v0.3.4).

### Transparency and automatic startup

Settings includes a 0–70% transparency slider with immediate preview. Save persists the corresponding 100–30% opacity in `settings.json`; Cancel restores the previous opacity. The default is fully opaque. Visibility, position and always-on-top remain independent preferences.

**Start automatically when I sign in to Windows** defaults to off. Saving a changed checkbox adds or removes only the `AI Mon` value in the current user's `Software\Microsoft\Windows\CurrentVersion\Run` registry key. No administrator rights are needed. The command quotes the executable and data folder and uses `--startup`, which leaves the detail window hidden while restoring the previous mini-window visibility. It respects external registry edits and rejects commands over Windows' 260-character Run limit. Windows Startup Apps policy/settings can separately disable launch; AI Mon does not override them.

The MSI's uninstall action asks the installed app to remove only a startup command belonging to that executable; upgrades preserve it. Portable users should uncheck startup before moving/deleting the executable. Tests use a separate temporary registry key, and automated UI tests disable startup registration with `--no-startup-registration`.

## Codex: account lookup without a conversation (0.3.5)

AI Mon starts the installed native `codex.exe app-server` in a hidden, bounded subprocess and calls `initialize`, `initialized`, and `account/rateLimits/read`. It does not start a thread or model turn. Lookup runs at startup and every two minutes while Codex monitoring is enabled, including when no session log changes. Refresh requests another lookup with a 15-second minimum gap. This schedule is independent of the local token scan interval.

Install and sign in to Codex CLI or the Codex VS Code extension. AI Mon discovers native executables from the standard standalone/npm locations, explicit PATH, or VS Code / VS Code Insiders extensions and prefers the most recently modified candidate. It uses the CLI's normal login and `CODEX_HOME`; the configured sessions folder affects local token totals only. AI Mon never reads credential files itself or stores raw RPC responses. The child has a 20-second deadline, a 2 MiB output cap, and a kill-on-close Windows job. Shutdown cancels the lookup.

Selection uses `rateLimitsByLimitId["codex"]`, independent of map order or the backward-compatible `rateLimits` field. For older CLIs without the map, `rateLimits` must explicitly identify `limitId: "codex"`; an unidentified legacy bucket stays unavailable. Model IDs, named model quotas, and model aliases are excluded. In particular, `codex_bengalfox` / GPT-5.3-Codex-Spark is not used for the main graphs. A missing main bucket clears the displayed windows. Malformed responses and network failures preserve the last successful snapshot and its original timestamp; the detail window reports lookup failure. A reported authentication-required error clears the old account quota and asks the user to sign in.

Remaining percentage is `100 - usedPercent`. Windows are classified by `windowDurationMins`: 10080 is weekly and up to one day is short-term. A weekly-only `primary` stays in the Weekly graph; missing windows stay blank. Session `rate_limits` records, including automated-review sessions, are never used for allowance. They remain eligible for ordinary token counting.

The [official account response schema](https://github.com/openai/codex/blob/main/codex-rs/app-server-protocol/schema/typescript/v2/GetAccountRateLimitsResponse.ts) defines the full snapshot and bucket map. Only normalized main-window values and a local fingerprint of `accountId` are persisted in `codex-quota.json`. When this fingerprint changes, Codex history is cleared rather than connecting values from different accounts. Older CLIs without an account ID cannot provide that account-change boundary.

## Claude: automatic lookup, including VS Code

Version 0.3.0 depended on the terminal status line, which does not run in the VS Code graphical extension. Version 0.3.1 queries the installed Claude Code executable at startup and every two minutes. Refresh requests an earlier lookup, with a 15-second minimum interval. Claude monitoring can be disabled in Settings.

The hidden, short-lived process exchanges `initialize` and `get_usage` (`skip_behaviors: true`) control messages over stream JSON. It sends no user/model prompt, disables hooks, tools, MCP servers, Chrome integration and session persistence for that subprocess, and leaves managed settings in effect. The official CLI handles authentication and network requests. AI Mon neither extracts credentials nor calls a private HTTP endpoint itself. This experimental control protocol was verified against installed Anthropic CLI 2.1.267 and VS Code extension 2.1.266; future protocol changes may require an update.

Executable discovery checks `%USERPROFILE%\.local\bin\claude.exe`, PATH, then the native binary bundled with the Anthropic VS Code extension. No CLI is bundled with AI Mon. The CLI has additional transient memory usage while querying; AI Mon's sub-1-MB distribution size does not include this already installed dependency. Each lookup has a 20-second deadline, a 2-MiB output limit, cancellation on app exit, and a job that cleans up its own child processes. Failed queries preserve the last known values and display an error. Missing CLI and accounts without reported subscription limits have separate messages.

`rate_limits.five_hour` and `seven_day` contain `utilization` percentages and ISO reset timestamps. AI Mon stores only normalized remaining percentages, reset times, the subscription type and observation time in `claude-quota.json`. Missing windows remain unknown.

## Claude: optional terminal status line

Open **Settings → Connect Claude status line**, then use Claude Code. Claude's [documented status-line input](https://code.claude.com/docs/en/statusline) supplies `rate_limits.five_hour` and `rate_limits.seven_day`, each with `used_percentage` and `resets_at`. Availability depends on the client version and subscription. Context-window occupancy is never treated as subscription allowance.

The connection updates the user `statusLine` entry in `CLAUDE_CONFIG_DIR/settings.json` or `%USERPROFILE%\.claude\settings.json`. Other settings are retained. The original entry is backed up under AI Mon's data folder. If an existing command is configured, AI Mon forwards the original JSON input to it through the same available shell (Git Bash, otherwise PowerShell) and preserves its output. Forwarded commands have a three-second timeout and their own child processes are cleaned up on timeout. Git Bash discovery honors `CLAUDE_CODE_GIT_BASH_PATH`, then the Git executable on PATH.

The bridge runs `ai-mon.exe --claude-statusline --data-dir <folder>` as a short-lived helper. It saves only normalized allowance values to `claude-quota.json`; source JSON, prompts, paths from the input, and credentials are not written into that file. Status-line updates during idle time may repeat the client's last-reported data; automatic lookup works independently of this optional bridge.

Use **Restore Claude status line** before moving/deleting a portable EXE or uninstalling AI Mon. This restores the original entry only if AI Mon's generated command is still in place, protecting subsequent user edits. Keep `claude-statusline-backup.json` until disconnected. If the executable was already removed, restore the backup's `original` value to Claude's `statusLine` setting manually, or remove that setting if `original` was null.

Project or managed settings can override the user status line. If the provider supplies no quota fields, the app does not estimate them from token counts. Both real Claude short-term and weekly account limits were retrieved successfully through the native executable in 0.3.1 testing. The optional bridge also has synthetic input and existing-command relay tests.

## Local storage and migration

`state.json` schema 3 persists up to 512 quota snapshots per provider alongside existing token events and checkpoints. Quota history retains up to seven days, keeps changes, and coalesces unchanged readings within 30 minutes. Migration from schema 1 or 2 preserves token events and checkpoints but discards old Codex quota history, which could contain mixed session/model values. Claude quota history from schema 2 is retained. New Codex history starts from account lookups. The existing 10 MiB state limit remains in effect.

The diagnostic `--scan` JSON includes `quota.observed`, `quota.plan`, and nullable `quota.short`/`quota.week`. Each window contains `remaining` in hundredths of one percent, `minutes`, and Unix `resets` (zero if unknown). For example, `remaining: 9500` means 95%. Missing data is `null`, not zero remaining.

Local token totals remain a separate secondary display. They are not a denominator for subscription quota or a billing statement.

## Validation

- `scripts/test.ps1`: quota parsing, expiry, latest-record selection, bounded history, cache migration, connection/restore behavior, plus existing parser/storage/language checks.
- `scripts/test-quota-bridge.ps1`: normalized storage, malformed input, separation from context occupancy, unchanged input forwarding, large pipe payloads, and child timeout.
- `scripts/test-quota-ui.ps1 -Language en|ko`: four charts, receipt of fixture quota data, 100 refreshes with stable GDI handle counts, screenshot capture, and normal shutdown on a private desktop.
- `scripts/test-codex-poll.ps1`: actual worker re-queries after two minutes with an empty sessions directory and a 300-second token scan interval, using an isolated fake Codex.
- `scripts/test-languages.ps1`: language switching and persistence on the redesigned screen.

The native account probe also retrieved the real main Codex quota while excluding a concurrently returned Spark quota. A separate Windows 10 device, all display scales, provider-account switching, and a 24-hour run remain unverified.

`--claude-probe --data-dir <folder>` performs one explicit CLI lookup for diagnostics. Exit 0 means at least one reported limit was saved; exit 4 indicates failure or unavailable limits. `--scan` remains a local-only diagnostic. `--codex-probe --data-dir <folder>` performs one Codex account lookup with the same exit codes. Use both `--no-claude-probe --no-codex-probe` for isolated UI fixtures; `--smoke-test` disables both automatically. `--scan` reads local logs and normalized quota files without launching either CLI.
