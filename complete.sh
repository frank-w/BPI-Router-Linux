#!/usr/bin/env bash
commands=$(grep '^\s*".*")$' build.sh | sed -e 's/^\s*"\(.*\)")$/\1/')
complete -W "$commands" ./build.sh
