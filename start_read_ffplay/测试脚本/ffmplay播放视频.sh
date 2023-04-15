#!/bin/zsh
ffpmeg_4_4_path=/usr/local/ffmpeg/4.4/bin
vidoe_path="/Users/dev/Desktop/mp4/金莎-爱的魔法.mp4"

cd $ffpmeg_4_4_path

# shellcheck disable=SC2154
$ffpmeg_4_4_path/ffplay $vidoe_path
