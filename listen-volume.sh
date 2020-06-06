#!/bin/bash
pactl subscribe | rg --line-buffered sink | xargs -n5 getvolume
