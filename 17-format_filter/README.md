### format: 视频滤镜:，用来转换图像的格式，
> 例如可以把 AV_PIX_FMT_YUV420P 转成 AV_PIX_FMT_RGB24。
>
* 查询 format 滤镜支持的参数
```shell
ffmpeg -hide_banner 1 -h filter=format
```
* 核心参数： pix_fmts

----
###  会议一下滤镜的API；
```c++

AVFilterContext main_buffer_ctx;
AVFilterContext result_buffersink_ctx;
AVFilterGraph  = avfilter_graph_alloc();
AVFilterInout;
AVBprint args;
 av_bprint_init(&args,0,AV_PRINT_ATOMIC);
av_bprint(&args,...);

avfilter_graph_prase2(avFilter,args.str,&input,&output);
avfilter_graph_config(avFilter,NULL);
main_buffer_ctx = avfilter_get_fiter(avFilterGraph,"Parsed_buffer_0");
result_buffersink_ctx = avfilter_get_fiter(avFilterGraph,"Parsed_sink_2");

// 获取数据
av_buffersrc_add_frame_flag(avFilterGraph,avFrame,buffersrc_flag_push);
av_buffersink_get_frame_flog(avFilterGraph,result_frame,buffersrc_flag_push);


```
