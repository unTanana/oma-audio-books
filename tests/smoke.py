#!/usr/bin/env python3
"""Real integration smoke. All media are SYNTHETIC; never use a personal library."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import sqlite3
import sys
import shutil
import tempfile
import time


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


with tempfile.TemporaryDirectory(prefix="oma-audio-books-synthetic-") as tmp, tempfile.TemporaryDirectory(prefix="oma-run-") as runtime:
    base = Path(tmp) / ("long-catalog-path-" * 5)
    library = base / "SYNTHETIC library"
    book = library / "Synthetic MP3 book"
    book.mkdir(parents=True)
    (library / ".oma-synthetic").write_text("SYNTHETIC integration media only\n")
    cover = base / "embedded.jpg"
    run("ffmpeg", "-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=32x32", "-frames:v", "1", str(cover))
    for number in (10, 2):
        run("ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=4",
            "-i", str(cover), "-map", "0:a", "-map", "1:v", "-c:v", "copy", "-disposition:v", "attached_pic",
            "-metadata", "album=Synthetic MP3 book", "-metadata", "artist=Synthetic Author",
            "-metadata", "series=Synthetic Series", "-metadata", "PART=2",
            "-y", str(book / f"{number}.mp3"))
    metadata = base / "chapters.txt"
    metadata.write_text(";FFMETADATA1\ntitle=Synthetic M4B book\nartist=Synthetic Author\n"
                        "[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=3000\ntitle=Synthetic One\n"
                        "[CHAPTER]\nTIMEBASE=1/1000\nSTART=3000\nEND=6000\ntitle=Synthetic Two\n")
    m4b = library / "Synthetic.m4b"
    run("ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=660:duration=6",
        "-i", str(metadata), "-map_metadata", "1", "-c:a", "aac", "-y", str(m4b))
    probe = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_chapters",
                                               "-of", "json", str(m4b)]))
    assert len(probe["chapters"]) == 2
    env = dict(os.environ, XDG_DATA_HOME=str(base / "data"), XDG_CONFIG_HOME=str(base / "config"),
               XDG_CACHE_HOME=str(base / "cache"), QT_QPA_PLATFORM="offscreen", QT_MEDIA_BACKEND="ffmpeg",
               QT_QPA_PLATFORMTHEME="offscreen", QT_NO_XDG_DESKTOP_PORTAL="1", QT_QUICK_CONTROLS_STYLE="Basic", OMA_HEADLESS="1",
               OMA_SMOKE_BASE=str(base), XDG_STATE_HOME=str(base / "state"), QT_QUICK_BACKEND="software",
               HOST_XDG_STATE_HOME=str(base / "state"), HOST_XDG_CONFIG_HOME=str(base / "config"), XDG_RUNTIME_DIR=runtime)
    executable = str(Path(sys.argv[1] if len(sys.argv) > 1 else "build/oma-audio-books").resolve())
    rejected_env = dict(env, XDG_DATA_HOME=str(base / "rejected-data"))
    rejected_env.pop("OMA_SMOKE_BASE")
    rejected = subprocess.run([executable, "--smoke", "import", str(library)], env=rejected_env, capture_output=True, timeout=10)
    assert rejected.returncode != 0 and not (base / "rejected-data").exists(), "unsafe smoke opened a catalog before refusing"
    run(executable, "--media-probe", str(book / "2.mp3"), str(m4b), env=env, timeout=30)
    print("PASS: SYNTHETIC MP3/M4B backend gate (not Audible compatibility)")
    run(executable, "--smoke", "import", str(library), env=env, timeout=40)
    database = base / "data/oma-audio-books/oma-audio-books/library.sqlite"
    assert database.exists(), "application did not create its catalog"
    with sqlite3.connect(database) as db:
        assert db.execute("select count(*) from books").fetchone()[0] == 2
        assert [Path(p).name for (p,) in db.execute("select path from tracks order by book_id, ordinal")][:2] == ["2.mp3", "10.mp3"]
    run(executable, "--smoke", "restart", str(library), env=env, timeout=30)
    modified = base / "modified.m4b"
    run("ffmpeg", "-v", "error", "-i", str(m4b), "-map", "0:a", "-c", "copy", "-map_chapters", "0",
        "-metadata", "artist=Rescanned Synthetic Author", "-y", str(modified))
    modified.replace(m4b)
    originals = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in library.rglob("*") if path.is_file()}
    run(executable, "--smoke", "features", str(library), env=env, timeout=40)
    assert all(hashlib.sha256(path.read_bytes()).hexdigest() == digest for path, digest in originals.items()), "app changed original media"
    with sqlite3.connect(database) as db:
        imported, overrides = db.execute("select imported,overrides from books where identity=?", (str(m4b),)).fetchone()
        assert json.loads(imported)["author"] == "Rescanned Synthetic Author"
        assert json.loads(overrides)["title"] == "My corrected synthetic title"
    for album, specs in [("Partial discs", [("Disc 1/1.mp3", "1", "1"), ("Disc 2/1.mp3", "1", "")]),
                         ("Tagged discs", [("Disc 1/2.mp3", "10", "1"), ("Disc 1/10.mp3", "2", "1"), ("Disc 2/1.mp3", "1", "2")]),
                         ("Ambiguous", [("1.mp3", "1", "1"), ("2.mp3", "2", "1")])]:
        for path, track, disc in specs:
            target = library / album / path
            target.parent.mkdir(parents=True, exist_ok=True)
            run("ffmpeg", "-v", "error", "-i", str(book / "2.mp3"), "-c", "copy",
                "-metadata", f"album={path if album == 'Ambiguous' else album}", "-metadata", f"track={track}",
                "-metadata", f"disc={disc}", "-y", str(target))
    shutil.copy(m4b, library / "Independent synthetic.m4b")
    outside = base / "outside"
    outside.mkdir()
    shutil.copy(m4b, outside / "Do not import.m4b")
    (library / "outside-link").symlink_to(outside, target_is_directory=True)
    (library / "cycle").symlink_to(library, target_is_directory=True)
    run(executable, "--smoke", "layouts", str(library), env=env, timeout=30)
    keyboard_book = library / "Synthetic keyboard book"
    keyboard_book.mkdir()
    for number in (1, 2):
        run("ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=45",
            "-metadata", "album=Synthetic keyboard book", "-metadata", "artist=Synthetic Author",
            "-y", str(keyboard_book / f"{number}.mp3"))
    theme = base / "state/omarchy/current/theme/colors.toml"
    theme.write_text('background = "#060b1e"\nforeground = "#ffcead"\naccent = "#7d82d9"\n')
    for shown in ("false", "true"):
        try:
            run(executable, "--ui-smoke", str(base / "ui.png"), env=env, timeout=25)
        finally:
            if os.environ.get("OMA_SMOKE_ARTIFACTS"):
                out = Path(os.environ["OMA_SMOKE_ARTIFACTS"])
                out.mkdir(parents=True, exist_ok=True)
                for path in base.glob("ui.png*"):
                    shutil.copy(path, out / path.name.removeprefix("ui.png."))
        assert f"seriesHeadings={shown}" in (base / "config/oma-audio-books/oma-audio-books.conf").read_text().splitlines(), "heading visibility not persisted across restarts"
    assert "sort=4" in (base / "config/oma-audio-books/oma-audio-books.conf").read_text().splitlines(), "Series preference not persisted"
    first = subprocess.Popen([executable], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        lock = database.with_name("instance.lock")
        while not lock.exists() and first.poll() is None and time.monotonic() < deadline:
            time.sleep(.05)
        assert lock.exists() and first.poll() is None, "first instance failed to start"
        while time.monotonic() < deadline:
            second = subprocess.run([executable], env=env, capture_output=True, timeout=5)
            if second.returncode == 0:
                break
            time.sleep(.1)
        assert second.returncode == 0 and first.poll() is None, "second-instance focus handoff failed with a long catalog path"
        print("PASS: second launch hands off to the running instance with a long catalog path")
    finally:
        first.terminate()
        first.wait(timeout=10)
