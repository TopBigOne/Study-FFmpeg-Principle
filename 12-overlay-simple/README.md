### 表示复杂指令

* -filter_complex

* overlay 滤镜

* NO.1: 将图标叠加于视频右上角：

```shell
ffmpeg -i ring.mp4 -i ring_100x87.png -filter_complex overlay=300:600 -max_muxing_queue_size 1024 result_ring_logo_t.mp4

```

* NO.2: 将图标叠加于视频右上角：
    * x = main_w-w
    * 意思是： 主背景的宽度-logo的宽度

```shell
ffmpeg -i ring.mp4 -i ring_100x87.png -filter_complex overlay=main_w-w:300 -max_muxing_queue_size 1024 result_ring_logo_t.mp4

```

### 参考资料

* [FFmpeg中overlay滤镜用法-水印及画中画](https://www.cnblogs.com/leisure_chn/p/10434209.html) 