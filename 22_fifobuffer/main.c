#include <stdio.h>

#include <libavutil/fifo.h>
#include <libavutil/avutil.h>

typedef struct MyData {
    long   b;
    int    a; //
    double c;

} MyData;


void my_code();

int main() {
    my_code();

    return 0;
}

void my_code() {

    printf(" int    : %lu\n", sizeof(int));
    printf(" long   : %lu\n", sizeof(long));
    printf(" double : %lu\n", sizeof(double));
    printf(" MyData : %zd\n", sizeof(MyData));

    // 有多少数据可读
    int read_space;
    // 有多少数据可写
    int write_space;


    AVFifoBuffer *list = av_fifo_alloc(sizeof(MyData) * 10);

    read_space  = av_fifo_size(list);
    write_space = av_fifo_space(list);

    av_log(NULL, AV_LOG_INFO, "read_space : %d ,write_space : %d,\n", read_space, write_space);

    av_fifo_grow(list, 5);
    av_log(NULL, AV_LOG_INFO, "read_space : %d ,write_space : %d,\n", read_space, write_space);


    MyData   myData;
    for (int i = 0; i < 5; ++i) {
        myData.a = 1;
        myData.b = 90000000;
        myData.c = 5.454545478;
        av_fifo_generic_write(list, &myData, sizeof(myData), NULL);

    }
    read_space  = av_fifo_size(list);
    write_space = av_fifo_space(list);
    av_log(NULL, AV_LOG_INFO, "read_space: %d,wirte_space: %d \n", read_space, write_space);


    MyData myData2;
    // 往myData2 读数据
    av_fifo_generic_read(list, &myData2, sizeof(myData2), NULL);
    av_log(NULL, AV_LOG_INFO, " myData2 info : %d, %ld, %f \n", myData2.a, myData2.b, myData2.c);

    av_fifo_generic_read(list, &myData2, sizeof(myData2), NULL);
    av_log(NULL, AV_LOG_INFO, " myData2 info : %d, %ld, %f \n", myData2.a, myData2.b, myData2.c);
    read_space  = av_fifo_size(list);
    write_space = av_fifo_space(list);

    av_log(NULL, AV_LOG_INFO, "read_space: %d,wirte_space: %d \n", read_space, write_space);


    // 释放list
    av_fifo_freep(&list);


}
