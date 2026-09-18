#!/usr/bin/env bash
# Create an offline source artifact and a checksum-pinned Arch recipe. No installation.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
version=0.1.0
mkdir -p dist
files=(CMakeLists.txt main.cpp app.cpp app.h scanner.cpp scanner.h desktop.cpp desktop.h
       smoke.cpp checks.h Main.qml tests/smoke.py tests/performance.cpp tests/scanner.cpp README.md IMPLEMENTATION.md LICENSE
       packaging/oma-audio-books.desktop packaging/oma-audio-books.svg packaging/PKGBUILD.in packaging/package.sh)
tar --transform="s,^,oma-audio-books-$version/," -czf "dist/oma-audio-books-$version.tar.gz" "${files[@]}"
read -r checksum _ < <(sha256sum "dist/oma-audio-books-$version.tar.gz")
sed "s/@SHA256@/$checksum/" packaging/PKGBUILD.in > dist/PKGBUILD
printf 'Created dist/oma-audio-books-%s.tar.gz and dist/PKGBUILD\n' "$version"
