### FFmpeg支持哪些封装(muxers)格式
### 查询命令
```shell
ffmpeg -hide_banner 1 -muxers
```
### 使用 相关API 获取所有的 封装格式
* av_demuxer_iterate()
* av_muxer_iterate()
```c++
#include <stdio.h>
#include "libavformat/avformat.h"

int main(){
    void* ifmt_opaque = NULL;
    const AVInputFormat *ifmt  = NULL;
    while ((ifmt = av_demuxer_iterate(&ifmt_opaque))) {
        printf("ifmt name is %s \n",ifmt->name);
    }

    void* ofmt_opaque = NULL;
    const AVOutputFormat *ofmt  = NULL;
    while ((ofmt = av_muxer_iterate(&ofmt_opaque))) {
        printf("ofmt name is %s \n",ofmt->name);
    }
    return 0;
}
```
> 注意 ifmt_opaque 跟 ofmt_opaque，这两个变量，这两个变量保存的是当前的迭代状态，刚开始遍历的时候需要传 NULL，他内部会改变这个指针的指向，指向当前的迭代状态。
> 简单来说，就是需要定义一个 void* 的 NULL 指针，传进去就行了。
### 扩展
* 如果需要遍历封装格式里面的参数，使用 [av_opt_next()](https://ffmpeg.xianwaizhiyin.net/api-ffmpeg/av_opt_next.html) 即可
