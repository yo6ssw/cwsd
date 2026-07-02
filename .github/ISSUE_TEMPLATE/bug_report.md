---
name: Bug report
about: Report a problem with cwsd
title: ''
labels: bug
assignees: ''
---

## What happened?

<!-- A clear description of the bug and what you expected instead. -->

## Environment

- Distribution / OS:
- `cwsd --version`:
- hamlib version (`rigctld --version`):
- Rig make/model and hamlib model number:
- Connection (USB serial device, network, …):

## Which service?

<!-- rigctld / cwdaemon / audio stream / remote_key — and the client talking to it
     (WSJT-X, fldigi, xlog2, …). -->

## Config

<!-- Relevant parts of your ~/.config/cwsdrc (redact anything private). -->

## Logs

<!-- Relevant lines from the file set in `logging.filename`. -->

> Tip: CAT can break **silently** after a hamlib upgrade — rebuild cwsd against
> the new hamlib before filing (see CLAUDE.md) if `rigctld` stopped responding
> after a system update.
