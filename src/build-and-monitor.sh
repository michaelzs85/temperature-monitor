#!/bin/bash

pio run --environment temperature-monitor-d1-mini --target upload && \
pio device monitor --baud 230400 --echo 
