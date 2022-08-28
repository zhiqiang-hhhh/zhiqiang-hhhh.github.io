
在OLTP系统中，B-tree 索引较为有效。但是在 OLAP 系统中，bit map indexes, cross table indexes, materialized view 会更有效。

Groups of columns sorted on the same attribute are referred to as “projections”; the same column may exist in multiple projections, possibly sorted on a different attribute in each. We expect that our aggressive compression techniques will allow us to support many column sort-orders without an explosion in space. The existence of multiple sort orders opens opportunities for optimization. 
