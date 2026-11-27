#!/usr/bin/env bash
# Rebuild examples/sample-paper.pdf from its LaTeX source.
set -euo pipefail
cd "$(dirname "$0")/../examples"
mkdir -p build
for _ in 1 2; do
    pdflatex -interaction=nonstopmode -halt-on-error \
        -output-directory=build sample-paper.tex >/dev/null
done
cp build/sample-paper.pdf sample-paper.pdf
rm -f build/sample-paper.aux build/sample-paper.log build/sample-paper.out \
      build/sample-paper.toc
printf 'built %s\n' "$(pwd)/sample-paper.pdf"
