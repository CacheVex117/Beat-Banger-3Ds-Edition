"""Entry point of the portable MOD2DS.exe (built by launcher/build_portable.py): `mod2ds oneshot` with the dropped files,
then wait for Enter so the window stays readable when it was opened by drag and drop."""
import sys
import traceback


def _utf8_console() -> None:
    if sys.platform == "win32":
        try:
            import ctypes
            ctypes.windll.kernel32.SetConsoleOutputCP(65001)
            ctypes.windll.kernel32.SetConsoleCP(65001)
        except Exception:
            pass
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def run() -> int:
    _utf8_console()
    from mod2ds.cli import main
    try:
        rc = main(["oneshot", *sys.argv[1:]])
    except SystemExit as exc:
        if isinstance(exc.code, str):
            print(exc.code)
        rc = exc.code if isinstance(exc.code, int) else 1
    except KeyboardInterrupt:
        rc = 1
    except Exception:
        traceback.print_exc()
        rc = 1
    try:
        input("\nPress Enter to close this window.")
    except EOFError:
        pass
    return rc


if __name__ == "__main__":
    sys.exit(run())
