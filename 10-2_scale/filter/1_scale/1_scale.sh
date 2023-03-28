#bash
video_path="/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-2_scale/raw_video/彈棉花的小花-頓啦愛你.mp4"

ffmpeg -i ${video_path} -vf scale=272:480 -y result_scale.mp4