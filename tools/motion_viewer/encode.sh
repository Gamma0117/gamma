#!/bin/bash
# encode rendered frames -> mp4 (1280x720) + gif (640x360)
cd /home/claude/art3d; O=out/anim; mkdir -p $O
for c in "$@"; do
  ffmpeg -y -loglevel error -framerate 30 -i render/$c/f%04d.jpg -c:v libx264 -pix_fmt yuv420p -crf 19 -preset slow -movflags +faststart $O/$c.mp4
  ffmpeg -y -loglevel error -framerate 30 -i render/$c/f%04d.jpg -vf "scale=480:270:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a:diff_mode=rectangle" $O/$c.gif
  ls -la $O/$c.*
done
