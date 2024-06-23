# 使用滤镜 获取视频的音量大小




----
# 额外收获
### 温故创建滤镜
### step 1:
* avfilter_get_by_name() ;获取一个滤镜的定义； 个人理解就是结构体
### step 2：
* 用 avfilter_graph_create_filter 一个一个地创建滤镜（AVFilterContext），
### step 3: 解析
* avfilter_graph_parse_str();
### step 4:  参数配置，并初始化
* avfilter_graph_config()
* 然后用 avfilter_link 函数把各个滤镜的输入输出连接起来，这种方式比较灵活，但是非常繁琐。

* 
