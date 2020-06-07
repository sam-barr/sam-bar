#!/bin/bash
getvolume
pactl subscribe | rg --line-buffered sink | xargs -L1 getvolume 2> /dev/null
