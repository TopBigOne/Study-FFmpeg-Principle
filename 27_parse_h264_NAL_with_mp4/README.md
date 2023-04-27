
* test h264
```shell
ffmpeg -i juren-30s.mp4 -vcodec h264 -y output.avi
```


### 参考
* [代码-读取 AnnexB 格式的 H.264 数据](https://www.zzsin.com/article/avc_3_read_annex_b.html)


### SEI
* 视频序列解码的增强信息
### PPS
* 图像参数集
### SPS
* 包含 的是针对一连续编码视频序列的参数，如标识符 seq_parameter_set_id、帧数及 POC 的约束、参考帧 数目、解码图像尺寸和帧场编码模式选择标识等等

> 通常，SPS 和 PPS 在片的头信息和数据解码前传送至解码器