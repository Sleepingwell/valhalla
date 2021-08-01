#!/bin/bash

#------------------------------------------------------------------------------
# Script for installing data for Vietnam and running a test 'snap to network'.
#------------------------------------------------------------------------------

# get data for Vietnam
wget https://download.geofabrik.de/asia/vietnam-latest.osm.pbf

#get the config and setup
mkdir -p valhalla_tiles
../scripts/valhalla_build_config \
    --service-limits-trace-max-distance 10000000 \
    --service-limits-trace-max-shape 1000000 \
    --mjolnir-data-processing-use-admin-db no \
    --mjolnir-tile-dir ${PWD}/valhalla_tiles \
    --mjolnir-tile-extract ${PWD}/valhalla_tiles.tar > valhalla.json

#build routing tiles
~/valhalla/build/valhalla_build_tiles -c valhalla.json vietnam-latest.osm.pbf

#tar it up for running the server
find valhalla_tiles | sort -n | tar cf valhalla_tiles.tar --no-recursion -T -

# and produce some output
../build/valhalla_service valhalla.json trace_attributes test.json
