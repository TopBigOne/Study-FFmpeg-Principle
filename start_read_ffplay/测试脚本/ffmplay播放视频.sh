#!/bin/zsh
ffpmeg_4_4_path=/usr/local/ffmpeg/4.4/bin
vidoe_path="/Users/dev/Desktop/mp4/金莎-爱的魔法.mp4"

cd $ffpmeg_4_4_path

# shellcheck disable=SC2154
$ffpmeg_4_4_path/ffplay  -x 400 -vf "drawtext=fontsize=200:fontfile=FreeSerif.ttf:text='FFmpeg':x=150:y=100" -vf "drawtext=fontsize=200:fontfile=FreeSerif.ttf:text='Principle':x=150:y=100" -i $vidoe_path
