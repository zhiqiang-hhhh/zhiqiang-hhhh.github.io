链接时常见的一类问题是符号的重复定义。如果该符号的定义确实在各个目标文件中一样，那么只需要在源码中将重复的定义删除就行了。但是如果由于各种原因，我们需要保留该符号的不同定义，同时还要要求链接器能够正确链接对应的不同版本，那问题就不好解决了。

首先必须了解，如果传递给链接器的重定位目标文件中有重复定义符号，那么链接器会报错；如果是多个静态链接库（.a）中有重复符号，那么链接不会报错，而是选择找到的第一个定义。以下文字描述的是如何解决问题2。

思路就是手动修改可重定位目标文件中的符号名称。参考 doris 如何处理对 bitshuffle 不同编译版本的链接：
```sh

# bitshuffle
build_bitshuffle() {
    check_if_source_exist $BITSHUFFLE_SOURCE
    cd $TP_SOURCE_DIR/$BITSHUFFLE_SOURCE
    PREFIX=$TP_INSTALL_DIR

    # This library has significant optimizations when built with -mavx2. However,
    # we still need to support non-AVX2-capable hardware. So, we build it twice,
    # once with the flag and once without, and use some linker tricks to
    # suffix the AVX2 symbols with '_avx2'.
    arches="default avx2"
    MACHINE_TYPE=$(uname -m)
    # Becuase aarch64 don't support avx2, disable it.
    if [[ "${MACHINE_TYPE}" == "aarch64" ]]; then
        arches="default"
    fi

    to_link=""
    for arch in $arches ; do
        arch_flag=""
        if [ "$arch" == "avx2" ]; then
            arch_flag="-mavx2"
        fi
        tmp_obj=bitshuffle_${arch}_tmp.o
        dst_obj=bitshuffle_${arch}.o
        ${CC:-$DORIS_GCC_HOME/bin/gcc} $EXTRA_CFLAGS $arch_flag -std=c99 -I$PREFIX/include/lz4/ -O3 -DNDEBUG -fPIC -c \
            "src/bitshuffle_core.c" \
            "src/bitshuffle.c" \
            "src/iochain.c"
        # Merge the object files together to produce a combined .o file.
        $DORIS_BIN_UTILS/ld -r -o $tmp_obj bitshuffle_core.o bitshuffle.o iochain.o
        # For the AVX2 symbols, suffix them.
        if [ "$arch" == "avx2" ]; then
            # Create a mapping file with '<old_sym> <suffixed_sym>' on each line.
            $DORIS_BIN_UTILS/nm --defined-only --extern-only $tmp_obj | while read addr type sym ; do
              echo ${sym} ${sym}_${arch}
            done > renames.txt
            $DORIS_BIN_UTILS/objcopy --redefine-syms=renames.txt $tmp_obj $dst_obj
        else
            mv $tmp_obj $dst_obj
        fi  
        to_link="$to_link $dst_obj"
    done
    rm -f libbitshuffle.a
    $DORIS_BIN_UTILS/ar rs libbitshuffle.a $to_link
    mkdir -p $PREFIX/include/bitshuffle
    cp libbitshuffle.a $PREFIX/lib/
    cp $TP_SOURCE_DIR/$BITSHUFFLE_SOURCE/src/bitshuffle.h $PREFIX/include/bitshuffle/bitshuffle.h
    cp $TP_SOURCE_DIR/$BITSHUFFLE_SOURCE/src/bitshuffle_core.h $PREFIX/include/bitshuffle/bitshuffle_core.h
}
```
源码中对应的改动：https://github.com/doris-vectorized/doris-vectorized/blob/2ce8e2524e9711567153ba84de77880978f72a55/be/src/olap/rowset/segment_v2/bitshuffle_wrapper.cpp
```c++
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "olap/rowset/segment_v2/bitshuffle_wrapper.h"

// Include the bitshuffle header once to get the default (non-AVX2)
// symbols.
#include <bitshuffle/bitshuffle.h>

#include "gutil/cpu.h"

// Include the bitshuffle header again, but this time importing the
// AVX2-compiled symbols by defining some macros.
#undef BITSHUFFLE_H
#define bshuf_compress_lz4_bound bshuf_compress_lz4_bound_avx2
#define bshuf_compress_lz4 bshuf_compress_lz4_avx2
#define bshuf_decompress_lz4 bshuf_decompress_lz4_avx2
#include <bitshuffle/bitshuffle.h> // NOLINT(*)
#undef bshuf_compress_lz4_bound
#undef bshuf_compress_lz4
#undef bshuf_decompress_lz4

using base::CPU;

namespace doris {
namespace bitshuffle {

// Function pointers which will be assigned the correct implementation
// for the runtime architecture.
namespace {
decltype(&bshuf_compress_lz4_bound) g_bshuf_compress_lz4_bound;
decltype(&bshuf_compress_lz4) g_bshuf_compress_lz4;
decltype(&bshuf_decompress_lz4) g_bshuf_decompress_lz4;
} // anonymous namespace

// When this translation unit is initialized, figure out the current CPU and
// assign the correct function for this architecture.
//
// This avoids an expensive 'cpuid' call in the hot path, and also avoids
// the cost of a 'std::once' call.
__attribute__((constructor)) void SelectBitshuffleFunctions() {
#if (defined(__i386) || defined(__x86_64__))
    if (CPU().has_avx2()) {
        g_bshuf_compress_lz4_bound = bshuf_compress_lz4_bound_avx2;
        g_bshuf_compress_lz4 = bshuf_compress_lz4_avx2;
        g_bshuf_decompress_lz4 = bshuf_decompress_lz4_avx2;
    } else {
        g_bshuf_compress_lz4_bound = bshuf_compress_lz4_bound;
        g_bshuf_compress_lz4 = bshuf_compress_lz4;
        g_bshuf_decompress_lz4 = bshuf_decompress_lz4;
    }
#else
    g_bshuf_compress_lz4_bound = bshuf_compress_lz4_bound;
    g_bshuf_compress_lz4 = bshuf_compress_lz4;
    g_bshuf_decompress_lz4 = bshuf_decompress_lz4;
#endif
}

int64_t compress_lz4(void* in, void* out, size_t size, size_t elem_size, size_t block_size) {
    return g_bshuf_compress_lz4(in, out, size, elem_size, block_size);
}
int64_t decompress_lz4(void* in, void* out, size_t size, size_t elem_size, size_t block_size) {
    return g_bshuf_decompress_lz4(in, out, size, elem_size, block_size);
}
size_t compress_lz4_bound(size_t size, size_t elem_size, size_t block_size) {
    return g_bshuf_compress_lz4_bound(size, elem_size, block_size);
}

} // namespace bitshuffle
} // namespace doris
```

简单总结：
```bash
# 打印所有外部可链接的定义完成的符号：
nm --defined-only --extern-only {objfile}  
# 重命名
echo {sym} {sym}_suffix > rename.txt
# 重新生成新的重定位目标文件
objcopy --redefine-syms=rename.txt {objfile} new_objfile
```