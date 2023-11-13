
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->

<!-- code_chunk_output -->

- [Runtime Filter](#runtime-filter)
  - [Principles](#principles)

<!-- /code_chunk_output -->


# Runtime Filter
## Principles
students
|id|name|age|
|--|--|--|
|1|A|10|
|2|B|12|
|3|C|11|
|4|D|13|
|5|E|9|

takes
|id|course|guard|
|--|--|--|
|1|Math|95|
|2|Language|91|
|3|Language|89|
|4|Painting|97|
|5|Physical|88|

`SELECT * FROM students JOIN takes ON students.id = taeks.id WHERE students.age > 10`

For above relation and query, we will have a typical query like:
```text
                  Join

    Sink 5 rows         Sink 4 rows
    Scan(takes)         Scan(students)
                            Filter(age > 10)
```
So for join build stage, we will get records like
```text
1   A   10
2   B   12
3   C   11
4   D   13
```
After we finished partition and build hash index for block from students, we will do probe stage by scanning fully data from takes table, it is 5 rows in total.

**Runtime filter aims to do filter on relation takes too.**

Look into detailes of scan result of students. **We can calculate a statistical index for the output block.** Statistical index is not a typical index like B+Tree index, statistical index includes bloom-filter index & minmix index and so on. Take minmax index as an example, we can get blow statistics for **join attributes**:
```text
min_id: 1
max_id: 4
```
If we can send such statistics to Scan node of takes, we can get a new execution plan like:
```text
                  Join
                        BuildRuntimeFilter
                        BuildStage

    Sink 4 rows         Sink 4 rows
    Scan(takes)         Scan(students)
    Filter              Filter(age > 10)
        WithMinmaxindex  
```
也就是说，我们可以减少Probe阶段需要的数据。原先需要 probe 5 行，现在通过minmax index过滤掉一行，就只需要 probe 4 行了。

这就是 Runtime Filter 的基本原理。