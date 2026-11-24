#!/bin/sh
# Builds DeckGalaxy (the bench's copy of the game) inside the rg-build container:
#   distrobox enter rg-build -- sh tools/bench/deck/build-in-container.sh
cd /run/media/deck/SD/Recomp/DeckGalaxy && python3 setup.py build
