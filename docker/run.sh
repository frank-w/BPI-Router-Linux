#!/bin/sh

DIR=$(dirname "$(readlink -f "$0")")
REPO_DIR="$DIR/.."

mkdir "$REPO_DIR/SD"
docker run -it -v "$REPO_DIR/SD":/SD -v "$REPO_DIR":/srv bpi-cross-compile:1 /bin/bash
