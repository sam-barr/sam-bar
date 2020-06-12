#!/bin/bash

# mac address of my headphones
MAC_ADDRESS=00:1B:66:AC:77:78

getvolume() {
    vol=$(pamixer --get-volume-human)
    vol=${vol/muted/mut}
    vol=${vol/100%/max}
    bt=$(bluetoothctl info "$MAC_ADDRESS" | sed -nE "s/\s*Connected: (\w)/\1/p")

    if [ "$vol" = "" ]; then
        echo "#1vol#4Err"
    elif [ "$bt" = "yes" ]; then
        echo "#1vol#3$vol"
    else
        echo "#1vol#1$vol"
    fi
}

getvolume
pactl subscribe | rg --line-buffered sink | while read -r; do getvolume; done
