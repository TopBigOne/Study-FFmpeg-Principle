#bash
# 使用图片做水印
# shellcheck disable=SC2034
video_path="/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-2_scale/raw_video/彈棉花的小花-頓啦愛你.mp4"
# shellcheck disable=SC2034
logo_path="/Users/dev/Documents/Android_work/main_ffmpeg/FFmpeg-Principle/10-2_scale/filter/logo.jpg"
ffmpeg -i ${video_path} -i ${logo_path} -filter_complex "[1:v]scale=1000:600[logo];[0:v][logo]overlay=x=600:y=0" result_filter.mp4
