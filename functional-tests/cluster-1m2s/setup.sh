#!/bin/sh

if [ ! -d ".venv" ]; then
    rm -rf .venv
fi

python3 -m venv .venv

. .venv/bin/activate

pip install -r requirements.txt

