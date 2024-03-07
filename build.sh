#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")" || exit
pipenv graph >/dev/null || pipenv install || exit
pipenv run python ./build.py "$@"
