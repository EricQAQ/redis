/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"

/* Note that these encodings are ordered, so:
 * INTSET_ENC_INT16 < INTSET_ENC_INT32 < INTSET_ENC_INT64. */
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/* Return the required encoding for the provided value. */
// 返回适用于v的编码方式所消耗的内存
static uint8_t _intsetValueEncoding(int64_t v) {
    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
}

/* Return the value at pos, given an encoding. */
// 根据给定的编码方式enc, 以及索引pos, 返回整数集合的底层数组对应的值
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) {
    int64_t v64;
    int32_t v32;
    int16_t v16;

    // (enc*)is->contents将底层数据转换为原有的编码类型
    // ((enc*)is->contents)+pos 找到对应元素在数组中正确的位置
    // 拷贝正确的值, 并进行大小端转换
    if (enc == INTSET_ENC_INT64) {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } else {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

/* Return the value at pos, using the configured encoding. */
// 根据集合的编码方式, 返回底层数组在pos索引上的值
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

/* Set the value at pos, using the configured encoding. */
// 根据整数集合的编码方式, 将底层数组在pos的位置上的值设置为value
static void _intsetSet(intset *is, int pos, int64_t value) {
    // 获取整数集合最新的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);

    // (encoding*)is->contents 根据最新的编码方式将底层数组转换为最新的数据
    // ((int64_t*)is->contents)[pos] 找到数组对应位置的地方
    // 在正确的位置进行赋值
    // 最后进行大小端转换
    if (encoding == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/* Create an empty intset. */
// 创建并返回一个新的intset(空整数集合)
intset *intsetNew(void) {
    // 为intset分配空间
    intset *is = zmalloc(sizeof(intset));
    // 设置初始编码
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    // 设置出事元素数量
    is->length = 0;
    return is;
}

/* Resize the intset */
// 调整整数集合的内存空间大小
static intset *intsetResize(intset *is, uint32_t len) {
    // 获取整数集合所需要的空间
    uint32_t size = len*intrev32ifbe(is->encoding);
    // 调整内存空间
    is = zrealloc(is,sizeof(intset)+size);
    return is;
}

/* Search for the position of "value". Return 1 when the value was found and
 * sets "pos" to the position of the value within the intset. Return 0 when
 * the value is not present in the intset and sets "pos" to the position
 * where "value" can be inserted. */
// 在整数集合is中找到值value对应的位置:
// 1. 如果找到了value, 则函数返回1, 并将pos设置为value在数组中的索引
// 2. 如果没有找到value, 则函数返回0, 并将pos设置为value可以插入的位置
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {
    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    int64_t cur = -1;

    /* The value can never be found when the set is empty */
    // 如果整数集合没有元素, 设置pos对应的值为0
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    } else {
        /* Check for the case where we know we cannot find the value,
         * but do know the insert position. */
        // 获取集合最后一个值, 并与value比较
        if (value > _intsetGet(is,intrev32ifbe(is->length)-1)) {
        // 因为数组是从小到大排列的, 如果value大于数组中最后一个值,
        // 则说明value肯定不存在于数组中, 那么插入位置肯定是末尾了
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        } else if (value < _intsetGet(is,0)) {
            // 获取集合第一个值, 并与value比较.
            // value小于集合中最小的值, 说明value肯定不存在与数组中,
            // 应该将value插入到数组的第一个
            if (pos) *pos = 0;
            return 0;
        }
    }

    // 使用二分法在数组中进行查找
    while(max >= min) {
        mid = ((unsigned int)min + (unsigned int)max) >> 1;
        cur = _intsetGet(is,mid);
        if (value > cur) {
            min = mid+1;
        } else if (value < cur) {
            max = mid-1;
        } else {
            break;
        }
    }

    // 检查是否找到value
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

/* Upgrades the intset to a larger encoding and inserts the given integer. */
// 根据value所使用的编码方式, 对整数集合的编码方式进行升级, 并将value插入整数集合
// 编码方式升级一共有个步骤:
// 1. 获取新旧编码方式
// 2. 根据新编码方式, 对整数集合进行扩容
// 3. 按照旧编码方式, 对contents里面的数据进行解析,
//    并从最后一个元素开始移动到重新编码后的位置
// 4. 安装新编码方式, 插入新值
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {
    // 获取当前的编码方式
    uint8_t curenc = intrev32ifbe(is->encoding);
    // 获取value所需要的编码方式
    uint8_t newenc = _intsetValueEncoding(value);
    // 获取整数集合当前的元素数量
    int length = intrev32ifbe(is->length);
    // 因为数据集是从小到大排序的, 所以新值也需要插入到正确的位置
    // 因为整数集合按照新值的编码升级了编码方式, 所以说明新值不是最大值就是
    // 最小值(正数和负数), 也就是说, 新值要么插入头部, 要么插入尾部)
    // 这时候就需要判断插入的位置在头部还是尾部了, prepend为1就是头部, 0就是尾部
    int prepend = value < 0 ? 1 : 0;

    /* First set new encoding and resize */
    // 更新集合的编码方式
    is->encoding = intrev32ifbe(newenc);
    // 根据新的编码方式调整整数集合的结构
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    /* Upgrade back-to-front so we don't overwrite values.
     * Note that the "prepend" variable is used to make sure we have an empty
     * space at either the beginning or the end of the intset. */
    // 按照旧编码方式, 对底层列表进行解析, 获取真实数据,
    // 并从最后一个元素开始移动从重新编码后的位置, 举例
    // 原整数集合: |00000000|00000001|00000000|00000011|00000000|00000110|, 使用int16_t进行编码(2 * 3)个字节, 需要插入 65535
    // 表示的值:   |--------1--------|--------3--------|--------10-------|
    // 1. 按照带插入的值的编码方式(int32_t)进行扩容, 扩容后变成(4 * 4)个字节
    // 扩容整数集合|00000000|00000001|00000000|00000011|00000000|00000110|00000000|00000000|00000000|00000000|00000000|00000000|00000000|00000000|00000000|00000000|
    // 2. 按照旧编码方式对原始数据进行解析:
    //             |00000000|00000001|00000000|00000011|00000000|00000110|00000000|00000000|00000000|00000000|00000000|00000000|00000000|00000000|00000000|00000000|
    //             |--------1--------|--------3--------|--------10-------|
    // 3. 从最后一个元素开始, 使用新编码方式, 填入正确的位置:
    //             |00000000|00000000|00000000|00000001|00000000|00000000|00000000|00000011|00000000|00000000|00000000|00000110|00000000|00000000|00000000|00000000|
    //             |-----------------1-----------------|-----------------3-----------------|-----------------10----------------|
    // 4. 插入新值:
    //             |00000000|00000000|00000000|00000001|00000000|00000000|00000000|00000011|00000000|00000000|00000000|00000110|00000000|00000000|11111111|11111111|
    //             |-----------------1-----------------|-----------------3-----------------|-----------------10----------------|----------------65535--------------|
    while(length--)
        // 根据原有的编码方式和索引依次找到数组原有的值, 并根据新编码方式, 把值移动到正确的位置
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));

    /* Set the value at the beginning or the end. */
    // 根据prepend来决定插入头部还是尾部, 插入新值
    if (prepend)    // 头部插入
        _intsetSet(is,0,value);
    else            // 尾部插入
        _intsetSet(is,intrev32ifbe(is->length),value);
    // 更新整数集合元素数量
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
    return is;
}

// 将整数集合is, 从索引from表示的这个元素开始, 到元素末尾, 移动到to这个索引所代表的位置
// 1. 向后移动:
//      假设数据列表为: | 1 | 2 | 4 | 5 | ? |    (?表示为新值准备的未设置的空间), 新值为3
//      那么就需要把从元素值为4(对应索引为2)开始往后的所有元素后移一位, 需要这么写:
//      intsetMoveTail(is, 2, 3);
// 2. 向前移动:
//      假设数据列表为: | 1 | 2 | 4 | 5 |, 需要删除的元素为2
//      那么久需要把从元素值为4(对应索引为2)开始往后的所有元素前移一位, 需要这么写:
//      intsetMoveTail(is, 2, 1);
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {
    void *src, *dst;
    // 获取需要移动的元素的个数
    uint32_t bytes = intrev32ifbe(is->length)-from;
    // 获取整数集合的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);

    // 根据编码的不同, 进行不同的操作
    if (encoding == INTSET_ENC_INT64) {
        // 记录移动的起始位置
        src = (int64_t*)is->contents+from;
        // 记录移动的结束为止
        dst = (int64_t*)is->contents+to;
        // 计算需要移动的字节数
        bytes *= sizeof(int64_t);
    } else if (encoding == INTSET_ENC_INT32) {
        src = (int32_t*)is->contents+from;
        dst = (int32_t*)is->contents+to;
        bytes *= sizeof(int32_t);
    } else {
        src = (int16_t*)is->contents+from;
        dst = (int16_t*)is->contents+to;
        bytes *= sizeof(int16_t);
    }
    // 进行移动
    memmove(dst,src,bytes);
}

/* Insert an integer in the intset */
// 将元素value插入整数集合is中, success用来记录是否插入成功, 成功为1, 失败为0
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {
    // 根据value来获取最合适的编码方式(int16, int32, int64)所消耗的内存
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    // 默认设置插入成功
    if (success) *success = 1;

    /* Upgrade encoding if necessary. If we need to upgrade, we know that
     * this value should be either appended (if > 0) or prepended (if < 0),
     * because it lies outside the range of existing values. */
    // 如果value的编码方式消耗的内存比整数集合现有的编码方式消耗的内存大,
    // 那么可以肯定value一定能插入到整数集合中
    // 那么需要升级整数集合现有的编码方式, 以满足value所要求的编码
    if (valenc > intrev32ifbe(is->encoding)) {
        /* This always succeeds, so we don't need to curry *success. */
        // 升级整数集合的编码方式, 按编码方式进行扩容, 更新原来的数据, 并插入新值
        return intsetUpgradeAndAdd(is,value);
    } else {
        // 说明整数集合现有的编码方式已经满足了新值value
        // 那么就在集合中查找新值value:
        // 1. 如果存在新值value, 则把success设置为0, 返回, 不做改动
        // 2. 如果不存在新值value, 则将插入的位置保存到pos指针中
        /* Abort if the value is already present in the set.
         * This call will populate "pos" with the right position to insert
         * the value when it cannot be found. */
        if (intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }

        // 集合中不存在value, 插入新值

        // 在集合中为value分配空间
        is = intsetResize(is,intrev32ifbe(is->length)+1);
        // 如果插入位置pos不在底层数组的末尾, 那么就需要对现有的元素进行移动, 空出新值的位置
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
    }

    // 将新值插入到底层数组对应的位置
    _intsetSet(is,pos,value);
    // 更新集合元素数量
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
    return is;
}

/* Delete integer from intset */
// 从整数集合is中删除值value, success用来记录删除是否成功
intset *intsetRemove(intset *is, int64_t value, int *success) {
    // 计算value的编码方式
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    // 默认删除失败
    if (success) *success = 0;

    // 如果value的编码大小<=整数集合使用的编码的大小, 则说明value有可能存在于集合中
    // 在这种情况下, 查找value所在的位置, 如果找到, 则删除该元素
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,&pos)) {
        uint32_t len = intrev32ifbe(is->length);

        /* We know we can delete */
        // 确定可以删除, 将标志设置为1
        if (success) *success = 1;

        /* Overwrite value with tail and update length */
        // 通过让value后面的所有元素向前移动一位, 来进行对value元素的删除
        if (pos < (len-1)) intsetMoveTail(is,pos+1,pos);
        // 缩小数组大小, 用来移除被删除的元素占用的空间
        is = intsetResize(is,len-1);
        // 更新集合的元素数量
        is->length = intrev32ifbe(len-1);
    }
    return is;
}

/* Determine whether a value belongs to this set */
// 判断给定的值value是否存在于整数集合is中
uint8_t intsetFind(intset *is, int64_t value) {
    // 获取value的编码大小
    uint8_t valenc = _intsetValueEncoding(value);
    // 如果value的编码大小<=当前集合使用的编码大小, 说明value有可能存在
    // 在集合中进一步查找value
    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,NULL);
}

/* Return random member */
// 随机返回一个元素
int64_t intsetRandom(intset *is) {
    return _intsetGet(is,rand()%intrev32ifbe(is->length));
}

/* Sets the value to the value at the given position. When this position is
 * out of range the function returns 0, when in range it returns 1. */
// 在整数集合中取出索引pos对应的值, 写入value中
// 注意这里的pos是底层数组的索引
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) {
    if (pos < intrev32ifbe(is->length)) {
        *value = _intsetGet(is,pos);
        return 1;
    }
    return 0;
}

/* Return intset length */
uint32_t intsetLen(intset *is) {
    return intrev32ifbe(is->length);
}

/* Return intset blob size in bytes. */
// 返回整数集合is占用的总字节大小
size_t intsetBlobLen(intset *is) {
    return sizeof(intset)+intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}

#ifdef REDIS_TEST
#include <sys/time.h>
#include <time.h>

#if 0
static void intsetRepr(intset *is) {
    for (uint32_t i = 0; i < intrev32ifbe(is->length); i++) {
        printf("%lld\n", (uint64_t)_intsetGet(is,i));
    }
    printf("\n");
}

static void error(char *err) {
    printf("%s\n", err);
    exit(1);
}
#endif

static void ok(void) {
    printf("OK\n");
}

static long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

#define assert(_e) ((_e)?(void)0:(_assert(#_e,__FILE__,__LINE__),exit(1)))
static void _assert(char *estr, char *file, int line) {
    printf("\n\n=== ASSERTION FAILED ===\n");
    printf("==> %s:%d '%s' is not true\n",file,line,estr);
}

static intset *createSet(int bits, int size) {
    uint64_t mask = (1<<bits)-1;
    uint64_t value;
    intset *is = intsetNew();

    for (int i = 0; i < size; i++) {
        if (bits > 32) {
            value = (rand()*rand()) & mask;
        } else {
            value = rand() & mask;
        }
        is = intsetAdd(is,value,NULL);
    }
    return is;
}

static void checkConsistency(intset *is) {
    for (uint32_t i = 0; i < (intrev32ifbe(is->length)-1); i++) {
        uint32_t encoding = intrev32ifbe(is->encoding);

        if (encoding == INTSET_ENC_INT16) {
            int16_t *i16 = (int16_t*)is->contents;
            assert(i16[i] < i16[i+1]);
        } else if (encoding == INTSET_ENC_INT32) {
            int32_t *i32 = (int32_t*)is->contents;
            assert(i32[i] < i32[i+1]);
        } else {
            int64_t *i64 = (int64_t*)is->contents;
            assert(i64[i] < i64[i+1]);
        }
    }
}

#define UNUSED(x) (void)(x)
int intsetTest(int argc, char **argv) {
    uint8_t success;
    int i;
    intset *is;
    srand(time(NULL));

    UNUSED(argc);
    UNUSED(argv);

    printf("Value encodings: "); {
        assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(-9223372036854775808ull) ==
                    INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+9223372036854775807ull) ==
                    INTSET_ENC_INT64);
        ok();
    }

    printf("Basic adding: "); {
        is = intsetNew();
        is = intsetAdd(is,5,&success); assert(success);
        is = intsetAdd(is,6,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(!success);
        ok();
    }

    printf("Large number of random adds: "); {
        uint32_t inserts = 0;
        is = intsetNew();
        for (i = 0; i < 1024; i++) {
            is = intsetAdd(is,rand()%0x800,&success);
            if (success) inserts++;
        }
        assert(intrev32ifbe(is->length) == inserts);
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int32: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,65535));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-65535));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int32 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Stress lookups: "); {
        long num = 100000, size = 10000;
        int i, bits = 20;
        long long start;
        is = createSet(bits,size);
        checkConsistency(is);

        start = usec();
        for (i = 0; i < num; i++) intsetSearch(is,rand() % ((1<<bits)-1),NULL);
        printf("%ld lookups, %ld element set, %lldusec\n",
               num,size,usec()-start);
    }

    printf("Stress add+delete: "); {
        int i, v1, v2;
        is = intsetNew();
        for (i = 0; i < 0xffff; i++) {
            v1 = rand() % 0xfff;
            is = intsetAdd(is,v1,NULL);
            assert(intsetFind(is,v1));

            v2 = rand() % 0xfff;
            is = intsetRemove(is,v2,NULL);
            assert(!intsetFind(is,v2));
        }
        checkConsistency(is);
        ok();
    }

    return 0;
}
#endif
