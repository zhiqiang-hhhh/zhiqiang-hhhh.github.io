## Key Concepts
### Targets
包括：executables、libraries、utilties built by CMake

add_library, add_executable, add_custom_target 都会创建一个 target

Targets store a list of libraries that they link against, which are set using the target_link_libraries command. Names passed into this command can be libraries, full paths to libraries, or the name of a library from an add_library command.

### Usage Requirements
CMake 会为 linked library targets 产生一个 usage requirements

Usage requirements affect compilation of sources in the \<target>. They are specified by properties defined on linked targets.
```cmake
add_library(foo foo.cxx)
target_include_directories(foo PUBLIC
                           "${CMAKE_CURRENT_BINARY_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           )
```
Now anything that links to the target foo will automatically have foo’s binary and source as include directories.

For each library or executable CMake creates, it tracks of all the libraries on which that target depends using the target_link_libraries command. For example:
```cmake
add_library(foo foo.cxx)
target_link_libraries(foo bar)

add_executable(foobar foobar.cxx)
target_link_libraries(foobar foo)
```
will link the libraries “foo” and “bar” into the executable “foobar” even though only “foo” was explicitly specified for it.

### Object Libraries
An Object Library is a collection of source files compiled into an object file which is not linked into a library file or made into an archive. 


## Properties
### INCLUDE_DIRECTORIES
对应
target_include_directories(Foo PUBLIC xxx)
target_include_directories(Foo PRIVATE xxx)

### INTERFACE_INCLUDE_DIRECTORIES
对应
target_include_directories(Foo PUBLIC xxx)
target_include_directories(Foo INTERFACE xxx)

当某个通过 target_link_libraries() 设置了 target dependencies 之后，cmake 会读取 dependencies 的该属性来确定 consumer 的头文件搜索路径。