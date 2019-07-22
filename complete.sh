#!/usr/bin/env bash

#needs to be sourced (e.g. ". ./complete.sh")
#based on https://iridakos.com/tutorials/2018/03/01/bash-programmable-completion-tutorial.html

_build_completions()
{
	buildscript=$(dirname $0)/build.sh
	options=$(sed -n -e '/case "$action" in/,/esac/{//!p}' $buildscript | grep '")$' | sed -e 's/^\W*\(.*\)")/\1/')
	for i in $options; do
		COMPREPLY+=("$i")
	done
	complete -W "$options" ./build.sh
}

complete -F _build_completions ./build.sh
