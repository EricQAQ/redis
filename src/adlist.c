/* adlist.c - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
// 创建一个新的链表
list *listCreate(void)
{
    struct list *list;

    // 为链表头部申请内存
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */
// 释放整个链表(包括链表头部, 以及全部节点)
void listRelease(list *list)
{
    unsigned long len;
    listNode *current, *next;

    current = list->head;   // 获取第一个节点
    len = list->len;
    // 遍历链表, 依次释放全部节点
    while(len--) {
        next = current->next;
        // 如果定义了链表节点的值释放函数, 使用该函数释放节点的值
        if (list->free) list->free(current->value);
        zfree(current);     // 释放节点结构
        current = next;
    }
    zfree(list);    // 释放链表头部
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
// 在双端链表头部插入节点, 值为value
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;

    // 为新节点申请内存空间
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    // 如果双端链表没有元素, 则把头节点和尾节点都设置为这个新节点
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        // 双端链表已经含有元素, 把新节点设置为第一个节点
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;    // 更新节点数量
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
// 在双端链表list的末尾插入新节点, 值为value
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    // 如果链表没有元素
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        // 链表有元素, 在尾部插入新节点, 更新最后一个节点信息
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

// 给双端链表list插入值为value的节点
// 如果after = 1, 就把新节点插到old_node的后面
// 如果after = 0, 就把新节点插到old_node的前面
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (after) {
        // after = 1, 把新节点插到old_node的后面
        node->prev = old_node;
        node->next = old_node->next;
        // old_node->next = node 这一步在下面做
        // 如果old_node原本是最后一个节点, 则更新双端链表的tail指针, 指向新节点
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        // after = 0, 把新节点插到old_node的前面
        node->next = old_node;
        node->prev = old_node->prev;
        // node->prev->next = node 这一步在下面做
        // 如果old_node原本是第一个节点, 则更新双端链表的head指针, 指向新节点
        if (list->head == old_node) {
            list->head = node;
        }
    }
    // 更新新节点的前置节点
    if (node->prev != NULL) {
        node->prev->next = node;
    }
    // 更新新节点的后置节点
    if (node->next != NULL) {
        node->next->prev = node;
    }
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
// 删除双端链表list的node节点
void listDelNode(list *list, listNode *node)
{
    if (node->prev)
        // 如果节点有前置节点, 则更新该前置节点的next指针
        node->prev->next = node->next;
    else
        // 节点没有前置节点, 说明是头节点, 更新双端链表头部的head指针
        list->head = node->next;
    if (node->next)
        // 如果节点有后置节点, 则更新改后置节点的prev指针
        node->next->prev = node->prev;
    else
        // 节点没有后置节点, 说明是尾节点, 更新双端链表头部的tail指针
        list->tail = node->prev;
    // 如果定义了节点的值的释放函数, 调用函数释放节点的值
    if (list->free) list->free(node->value);
    zfree(node);    // 释放节点结构
    list->len--;    // 更新节点长度信息
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
// 给双端链表list创建一个迭代器, 在调用这个函数之后, 每次调用这个迭代器
// 的next方法, 都会返回被迭代到的节点的下一个节点
// direction参数用来控制迭代的方向,
//      1. AL_START_HEAD表示从头到尾正向迭代
//      2. AL_START_TAIL表示从尾到头逆向迭代
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;

    // 为链表创建迭代器
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    // 根据迭代方向设置迭代器的next值
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
// 释放迭代器
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */
// 把迭代器的迭代方向设置为正向迭代, 并从链表第一个元素开始
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

// 把迭代器的迭代方向设置为逆向迭代, 并从链表最后一个元素开始
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */
// 获取迭代器指向的下一个节点
// 允许使用listDelNode函数来删除当前节点, 但是不能更改链表的其他节点
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;     // 下一个节点

    // 如果该节点不为空, 则根据迭代器的迭代方向更新迭代器的next指针
    // 防止因为当前节点被删除而导致的指针丢失的情况
    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
// 复制整个双端链表
list *listDup(list *orig)
{
    list *copy;
    listIter iter;
    listNode *node;

    // 创建新链表
    if ((copy = listCreate()) == NULL)
        return NULL;
    // 复制链表节点处理函数
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    // 设置原链表的正向迭代器
    listRewind(orig, &iter);
    // 使用迭代器遍历原链表
    while((node = listNext(&iter)) != NULL) {
        void *value;

        // 如果定义了节点值的复制函数, 就调用该函数复制节点的值
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                return NULL;
            }
        } else
            // 没有定义节点值的复制函数, 直接复制节点值
            value = node->value;
        // 给新链表插入值
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            return NULL;
        }
    }
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
// 查找链表list中节点的值value和key相匹配的节点,
// 如果找到该节点, 则返回指向该节点的指针, 如果没有则返回null
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;
    listNode *node;

    listRewind(list, &iter);    // 设置正向迭代器
    // 遍历链表查找
    while((node = listNext(&iter)) != NULL) {
        // 如果设置了节点值的比较函数, 使用该函数来比较key和对应节点的值
        if (list->match) {
            if (list->match(node->value, key)) {
                return node;
            }
        } else {
            // 没有设置match函数, 直接比较
            if (key == node->value) {
                return node;
            }
        }
    }
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
// 根据给予的索引index返回链表对应的节点
listNode *listIndex(list *list, long index) {
    listNode *n;

    if (index < 0) {
        // 如果索引为负数, 则逆向查找链表
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        // 如果索引为正数, 则正向查找链表
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
// 取出链表末尾节点, 并设置为链表的第一个节点
void listRotate(list *list) {
    listNode *tail = list->tail;

    if (listLength(list) <= 1) return;

    /* Detach current tail */
    list->tail = tail->prev;
    list->tail->next = NULL;
    /* Move it as head */
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}
