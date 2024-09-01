#!/bin/sh

DIR=$(dirname "$(readlink -f "$0")")
REPO_DIR="$DIR/../.."

export UID=$(id -u)
export GID=$(id -g)

mkdir -p "$REPO_DIR/SD"
docker run --privileged -it \
	--user $UID:$GID \
	-v "$REPO_DIR/SD":/srv/SD \
	-v "$REPO_DIR":/srv/code \
	bpi-cross-compile:1 /bin/bash
