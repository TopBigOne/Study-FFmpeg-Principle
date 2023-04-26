* 
* 
* 
* 将AVCC格式的H.264流的转换为Annex-B格式，可以使用以下命令

```shell
ffmpeg -i juren-30s.mp4 -codec copy -bsf:v h264_mp4toannexb doc/OUTPUT.ts
```
