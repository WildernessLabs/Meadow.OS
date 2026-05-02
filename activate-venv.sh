#!/bin/bash -ex

#
#   Create a Python virtual environment and install the required dependencies for pynuttx.
#


if [ ! -d ".venv" ]; then
	python3 -m venv .venv
    #
    #   Update pip and install the required dependencies for pynuttx.
    #
    python3 -m pip install --upgrade pip
    python3 -m pip install -r nuttx/tools/pynuttx/requirements.txt
fi
source .venv/bin/activate