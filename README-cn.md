# Redis 3.2.11源码注释

## 1. Redis几种数据结构的实现

1. redis中数据类型string的实现: sds.h, sds.c
2. redis中数据类型list的实现:
    1. 3.2以前的版本, 主要有两种数据结构:
        -. 压缩列表ziplist: ziplist.h, ziplist.c,
        -. 双端链表: adlist.h, adlist.c
    2. 3.2版本, 一种数据结构(quicklist): quicklist.h, quicklist.c
3. redis中数据类型set的实现:
    1. 整数集合intset: intset.h, intset.c
    2. 字典dict: dict.h, dict.c
4. redis中数据类型hash的实现:
    1. 压缩列表ziplist
    2. 字典dict
5. redis中数据类型sortedset的实现:
    1. 压缩列表ziplist
    2. 跳跃表skiplist: server.h, t_zset.c
