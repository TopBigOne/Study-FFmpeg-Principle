###  [源码位置](https://github.com/lokenetwork/FFmpeg-Principle/blob/main/output/main.c)
### 学而时习之，温故一下AVIO

### 核心函数
#### step 1 ： avformat_alloc_output_context2（）
*  Allocate an AVFormatContext for an output format.
```c
int avformat_alloc_output_context2(AVFormatContext **ctx, ff_const59 AVOutputFormat *oformat,const char *format_name, const char *filename);
```
#### step 2 : avio_open2()
#### step 3 : avformat_write_header()
#### step 4 : av_interleaved_write_frame()
#### step 5 : av_write_trailer()