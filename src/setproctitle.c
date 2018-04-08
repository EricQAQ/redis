/* ==========================================================================
 * setproctitle.c - Linux/Darwin setproctitle.
 * --------------------------------------------------------------------------
 * Copyright (C) 2010  William Ahern
 * Copyright (C) 2013  Salvatore Sanfilippo
 * Copyright (C) 2013  Stam He
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>	/* NULL size_t */
#include <stdarg.h>	/* va_list va_start va_end */
#include <stdlib.h>	/* malloc(3) setenv(3) clearenv(3) setproctitle(3) getprogname(3) */
#include <stdio.h>	/* vsnprintf(3) snprintf(3) */

#include <string.h>	/* strlen(3) strchr(3) strdup(3) memset(3) memcpy(3) */

#include <errno.h>	/* errno program_invocation_name program_invocation_short_name */

#if !defined(HAVE_SETPROCTITLE)
#define HAVE_SETPROCTITLE (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__)
#endif


#if !HAVE_SETPROCTITLE
#if (defined __linux || defined __APPLE__)

extern char **environ;

static struct {
	/* original value */
	const char *arg0;

	/* title space available */
	char *base, *end;

	 /* pointer to original nul character within base */
	char *nul;

	_Bool reset;
	int error;
} SPT;


#ifndef SPT_MIN
#define SPT_MIN(a, b) (((a) < (b))? (a) : (b))
#endif

static inline size_t spt_min(size_t a, size_t b) {
	return SPT_MIN(a, b);
} /* spt_min() */


/*
 * For discussion on the portability of the various methods, see
 * http://lists.freebsd.org/pipermail/freebsd-stable/2008-June/043136.html
 */
// 清除environ中的环境参数
static int spt_clearenv(void) {
#if __GLIBC__
	clearenv();

	return 0;
#else
	extern char **environ;
	static char **tmp;

	if (!(tmp = malloc(sizeof *tmp)))
		return errno;

	tmp[0]  = NULL;
	environ = tmp;

	return 0;
#endif
} /* spt_clearenv() */


// 清除原有的environ, 根据给予的环境参数oldenv, 重新写入environ
// 这样做的目的是为了给environ重新分配内存空间, 使得如果更新了进程名称,
// 并且更新后的名称长度大于原有长度的时候, 不会导致崩溃
static int spt_copyenv(char *oldenv[]) {
	extern char **environ;
	char *eq;
	int i, error;

	if (environ != oldenv)
		return 0;

    // 清除environ的环境参数
	if ((error = spt_clearenv()))
		goto error;

    // 遍历oldenv获取环境参数, 并重新设置环境参数
    // 因为在spt_init函数中, oldenv保存了environ指向的地址,
    // 上一步调用spt_clearenv函数后, environ被置为了NULL,
    // 但是oldenv的内容依然有效
    // 所以上一步清除environ并不会影响到这一步的遍历
	for (i = 0; oldenv[i]; i++) {
        // 找到=所在的位置
		if (!(eq = strchr(oldenv[i], '=')))
			continue;

        // 将=设置为空白符, 用于切断语句
		*eq = '\0';
        // 重新设置环境变量
		error = (0 != setenv(oldenv[i], eq + 1, 1))? errno : 0;
		*eq = '=';

		if (error)
			goto error;
	}

	return 0;
error:
	environ = oldenv;

	return error;
} /* spt_copyenv() */


static int spt_copyargs(int argc, char *argv[]) {
	char *tmp;
	int i;

	for (i = 1; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i])
			continue;

		if (!(tmp = strdup(argv[i])))
			return errno;

		argv[i] = tmp;
	}

	return 0;
} /* spt_copyargs() */


void spt_init(int argc, char *argv[]) {
    // C中,可以使用全局变量char **environ来访问环境列表,
    // 与main函数接受的argv类似
        char **envp = environ;
	char *base, *end, *nul, *tmp;
	int i, error;

    // 把base指向argv[0]
	if (!(base = argv[0]))
		return;

	nul = &base[strlen(base)];
	end = nul + 1;

	for (i = 0; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i] || argv[i] < end)
			continue;

		end = argv[i] + strlen(argv[i]) + 1;
	}

	for (i = 0; envp[i]; i++) {
		if (envp[i] < end)
			continue;

		end = envp[i] + strlen(envp[i]) + 1;
	}

    // 设置SPT的arg0参数, 也就是进程名称
	if (!(SPT.arg0 = strdup(argv[0])))
		goto syerr;

#if __GLIBC__
    // gcc下获取程序名可以通过program_invocation_name变量来获得
    // program_invocation_name是进程被命令行启动时调用的名字, 即argv[0]
    // program_invocation_short_name是program_invocation_name末尾不含目录文件名的部分
    // strdup用来拷贝字符串的副本, 有独立的内存空间
	if (!(tmp = strdup(program_invocation_name)))
		goto syerr;

	program_invocation_name = tmp;

	if (!(tmp = strdup(program_invocation_short_name)))
		goto syerr;

    // 设置redis-server进程名称
	program_invocation_short_name = tmp;
#elif __APPLE__
	if (!(tmp = strdup(getprogname())))
		goto syerr;

	setprogname(tmp);
#endif


    // 下面两步的主要目的是: 为环境参数和argv重新分配内存空间
	if ((error = spt_copyenv(envp)))
		goto error;

	if ((error = spt_copyargs(argc, argv)))
		goto error;

	SPT.nul  = nul;
    // 因为base指针已经指向了argv[0], 通过这步操作, 可以让我们在修改
    // 进程名称的时候, 直接改SPT.base指向的值就可以了
	SPT.base = base;
	SPT.end  = end;

	return;
syerr:
	error = errno;
error:
	SPT.error = error;
} /* spt_init() */


#ifndef SPT_MAXTITLE
#define SPT_MAXTITLE 255
#endif

// 根据给定的格式和对应的参数, 更新server的进程名称
// 进程的名称, 实际上就是main函数里面的argv[0], 如果需要更新进程名称, 修改那段
// 内存空间的内容就可以了.
// 但是为了让程序的正常逻辑不受影响, 需要在不改变访问的方式下, 把原来的内容保存起来
// argv[0]和环境参数environ所指向的内存空间是连续的(如下),
// 在程序启动的时候, 这些空间就已经被设置好了.
// argv    ---> 0x12345670 ---> ./redis-server
//         ---> 0x12345674 ---> arg0
//         ---> 0x12345678 ---> arg1
//         ---> 0x1234567c ---> arg2
//         ---> NULL
// environ ---> 0xa2345670 ---> Environ1=val1
//         ---> 0xa2345674 ---> Environ2=val2
//         ---> 0xa2345678 ---> Environ3=val3
//         ---> 0xa234567c ---> Environ4=val4
//         ---> NULL
//
// 更改进程名称需要做的, 
// 1. 重新分配空间来存储这些内容(不需要连续), 然后把地址分别复制给argv和environ
//    数组, 这样原来的访问方式就不受影响了
// 2. 在argv开始的地方和environ结束的地方, 设置内容
void setproctitle(const char *fmt, ...) {
	char buf[SPT_MAXTITLE + 1]; /* use buffer in case argv[0] is passed */
	va_list ap;
	char *nul;
	int len, error;

	if (!SPT.base)
		return;

    // 根据传递进来的格式, 以及参数, 构造新的进程名称, 写入buf中
	if (fmt) {
		va_start(ap, fmt);
		len = vsnprintf(buf, sizeof buf, fmt, ap);
		va_end(ap);
	} else {
		len = snprintf(buf, sizeof buf, "%s", SPT.arg0);
	}

	if (len <= 0)
		{ error = errno; goto error; }

    // 如果没有reset过, 就把原有的进程名称清空
	if (!SPT.reset) {
		memset(SPT.base, 0, SPT.end - SPT.base);
		SPT.reset = 1;
	} else {
        // 如果reset过, 就把新的进程名称, 设置到SPT.base中
		memset(SPT.base, 0, spt_min(sizeof buf, SPT.end - SPT.base));
	}

	len = spt_min(len, spt_min(sizeof buf, SPT.end - SPT.base) - 1);
    // 修改进程名称(因为SPT.base指向了argv[0])
	memcpy(SPT.base, buf, len);
	nul = &SPT.base[len];

	if (nul < SPT.nul) {
		*SPT.nul = '.';
	} else if (nul == SPT.nul && &nul[1] < SPT.end) {
		*SPT.nul = ' ';
		*++nul = '\0';
	}

	return;
error:
	SPT.error = error;
} /* setproctitle() */


#endif /* __linux || __APPLE__ */
#endif /* !HAVE_SETPROCTITLE */
