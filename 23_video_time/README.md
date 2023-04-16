### 温故一下视频，时间基
* 如何计算一个视频的时长？
```c
    AVStream *video_stream = avFormatContext->streams[0];
    int64_t  duration      = video_stream->duration;
    int64_t  second_times  = duration * av_q2d(video_stream->time_base);
    printf("这个视频的时长: %lld s.\n", second_times);
```