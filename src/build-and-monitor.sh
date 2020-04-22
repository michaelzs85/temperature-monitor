#!/bin/bash

pio run --environment temp-mon-d1-mini --target upload && \
pio device monitor -baud 230400 --echo 

