#bash
video_path="/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-2_scale/raw_video/彈棉花的小花-頓啦愛你.mp4"
ffmpeg -i ${video_path} -vf crop=720:360:0:140,scale=-2:480 -c:v libx265 -c:a libmp3lame -y result_crop.mp4
