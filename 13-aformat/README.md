### aformat : 音频格式滤镜

* 命令查询 aformat 滤镜支持的参数：

```shell
ffmpeg -hide_banner 1 -h filter=aformat
```

* aformat 滤镜支持 3 个参数:
    * sample_fmts （采样格式）
    * sample_rates （采样率）
    * channel_layouts （声道布局）