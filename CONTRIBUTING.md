# Development

Keep this repository limited to source code, documentation, and generated test logic.
Use synthetic signals for committed fixtures. Keep recordings, recovered payloads,
receiver settings, hardware serial numbers, local paths, and crash dumps outside Git.
The ignored `local/` directory is available for private development results.

Build against the matching SDRangel headers, receive-sample representation, and
runtime libraries. Run `python tests/validate_core.py` after building the core CLI.
The test dependencies are listed in `requirements-test.txt`.

Native channel and GUI integration helpers require the matching runtime. Use a
separate test process with no physical SDR attached; the control port must be free.
Build output, symbols, and diagnostic logs must remain untracked.
