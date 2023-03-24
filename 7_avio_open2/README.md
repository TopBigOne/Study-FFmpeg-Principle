[avio_open2](https://ffmpeg.xianwaizhiyin.net/api-ffmpeg/output.html) : 打开一个输出文件


### 主要步骤
* step 1:
  * avio_open2() // 打开输出文件
* step 2: 
  * avformat_write_header() // 写入头信息
  
* step 3:
  * 循环 av_interleaved_write_frame() // write a packet to an output media file
  * 循环 av_rescale_q_rnd() // 时间转换
* step 4:
  * av_write_trailer() // Write the stream trailer to an output media file
* step 5:
  * avio_closep() // Close the resource accessed by the AVIOContext *s