
### H265编码参数设置[I帧间隔、B帧个数(B帧决策类型)、crf、码率配置等]

* scenecut=0  表示场景切换关闭；
* keyint=50   表示I帧间隔为50(即gop为50)；
* b-adapt=0   表示b帧类型决策关闭，采用固定b帧个数；
* bframes=3   表示b帧个数为3;
* bitrate=500  表示编码码率为500k
```shell
ffmpeg  -i "平凡之路MV（朴树演唱）.mp4" -r 50 -vcodec libx265 -x265-params "scenecut=0:keyint=50:b-adapt=0:bframes=3:bitrate=500" "./result_video/平凡之路MV（朴树演唱）.mp4"
```

