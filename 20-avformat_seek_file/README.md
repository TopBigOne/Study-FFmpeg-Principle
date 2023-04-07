### avformat_seek_file()  快速seek 到流媒体文件的某一个位置
* avformat_seek_file() 函数的定义如下： 6个参数
```c++
int avformat_seek_file(AVFormatContext *s, int stream_index, int64_t min_ts, int64_t ts, int64_t max_ts, int flags);

```
### 参数解释如下：

1. AVFormatContext *s，已经打开的容器示例。

2. int **stream_index**，流索引，但是只有在 flags 包含 AVSEEK_FLAG_FRAME 的时候才是 设置某个流的读取位置。其他情况都只是把这个流的 time_base （时间基）作为参考。

3. int64_t **min_ts**，跳转到的最小的时间，但是这个变量不一定是时间单位，也有可能是字节单位，也可能是帧数单位（第几帧）。

4. int64_t **ts**，要跳转到的读取位置，单位同上。

5. int64_t **max_ts**，跳转到的最大的时间，单位同上，通常填 INT64_MAX 即可。

6. int **flags**，跳转的方式，有 4 个 flags，如下：
   * **AVSEEK_FLAG_BYTE**，按字节大小进行跳转。
   * **AVSEEK_FLAG_FRAME**，按帧数大小进行跳转。
   * **AVSEEK_FLAG_ANY**，可以跳转到非关键帧的读取位置，但是解码会出现马赛克。
   * **AVSEEK_FLAG_BACKWARD**，往 ts 的后面找关键帧，默认是往 ts 的前面找关键帧。
## ts 和 
### case 1: 当 flags 为 0 的时候:
* 默认情况，是按 时间来 seek 的，而时间基是根据 stream_index 来确定的。
* 如果 stream_index 为 -1 ，那 ts 的时间基就是 AV_TIME_BASE，
* 如果stream_index 不等于 -1 ，那 ts 的时间基就是 stream_index 对应的流的时间基。
* 这种情况，avformat_seek_file() 会导致容器里面所有流的读取位置都发生跳转，包括音频流，视频流，字幕流

### case 2: 当 flags 为 AVSEEK_FLAG_BYTE :
* ts 参数就是字节大小，代表 avformat_seek_file() 会把读取位置设置到第几个字节。
* 用 av_read_frame() 读出来的 pkt 里面有一个字段 pos，
* 代表当前读取的字节位置。可以用pkt->pos 辅助设置 ts 参数，

### case 3: 当 flags 为 AVSEEK_FLAG_FRAME :
* ts 参数就是帧数大小，**代表 avformat_seek_file() 会把读取位置设置到第几帧**。
* 这时候 stream_index 可以指定只设置某个流的读取位置，
* 如果 stream_index 为 -1 ，代表设置所有的流。
### case 4: 当 flags 为 AVSEEK_FLAG_ANY :
* 那就代表 seek 可以跳转到非关键帧的位置，但是非关键帧解码会出现马赛克。
* 如果不设置 AVSEEK_FLAG_ANY， 默认是跳转到离 ts 最近的关键帧的位置的。
### case 5: 当 flags 为 AVSEEK_FLAG_BACKWARD :
* 代表 avformat_seek_file() 在查找里 ts 最近的关键帧的时候，
* 会往 ts 的后面找，默认是往 ts 的前面找关键帧。
### PS：AVSEEK_FLAG_BYTE ，AVSEEK_FLAG_FRAME，AVSEEK_FLAG_ANY 这 3 种方式，有些封装格式是不支持的。






### 参考
* https://ffmpeg.xianwaizhiyin.net/api-ffmpeg/avformat_seek_file.html
* 


