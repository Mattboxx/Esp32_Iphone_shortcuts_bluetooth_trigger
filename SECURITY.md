# Security policy

## Reporting a vulnerability

Please **do not open a public issue** for security problems.
Use GitHub's private reporting instead:
**Security** tab → **Report a vulnerability**, or write to the maintainer via the
contact on the [GitHub profile](https://github.com/Mattboxx).

You'll get an answer within a few days. Fixes ship as a new firmware release and
web installer update; reporters are credited unless they prefer otherwise.

## Scope

- Firmware in `src/` (Bluetooth stack configuration, pairing, settings parsing).
- Web installer in `docs/` (served from GitHub Pages).
- Build and release workflows in `.github/workflows/`.

## Known design limits (not vulnerabilities)

- **Just Works pairing.** The board has no screen or keypad, so pairing uses
  Bluetooth "Just Works": anyone nearby can pair while pairing is open. Use the
  **pairing window** setting to accept new phones only for 60 s / 3 min after
  power-on; already paired phones always reconnect. Hold the reset button 5 s to
  forget every phone.
- **No input is ever sent.** The device presents a mouse but never sends
  reports: it cannot type, click or move anything on the phone.
- **Physical access = full control.** Anyone with the board and a USB cable can
  reflash it or read its settings. Flash encryption / secure boot are not
  enabled, so the web installer can update boards in one click.

## How releases are protected

- Firmware images are built by CI from the tagged source; the web installer
  checks each download against the SHA-256 in `manifest.json` before flashing,
  and Releases include `SHA256SUMS.txt`.
- The installer only runs its own files (strict Content-Security-Policy, no CDN,
  esptool-js vendored).
- Workflows use a read-only token by default and pin every action to a commit SHA.
