### Date/DateTimeComputation

```cpp {.line-numbers}
template <typename Transform>
class FunctionDateOrDateTimeComputation : public IFunction {
public:
    ...
    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        size_t result, size_t input_rows_count) const override {
        
    }
}


template <typename T>
template <TimeUnit unit, typename TO>
bool DateV2Value<T>::date_add_interval(const TimeInterval& interval, DateV2Value<TO>& to_value) {
    ...
}

```
```sql
BUG:
select date_add(date '9999-02-02', interval  15900000 minute);
select date_add(date '2023-02-02', interval  1591431080 year);
select date_add(date '2023-02-02', interval  1591431080 hour);
select date_add(date '9999-02-02', interval  15900000014310080 second);
select date_add(date '9999-02-02', interval  1590000001431080 second); ->  9961-06-28 04:23:04


NULL:
select date_add(date '2023-02-02', interval  1591431080 day);
select date_add(date '2023-02-02', interval  1591431080 month);
select date_add(date '2023-02-02', interval  1591431080 week);
```
FE 上常量折叠计算 date_add
```java
public class DateTimeArithmetic {
    /**
     * datetime arithmetic function date-add.
     */
    @ExecFunction(name = "date_add", argTypes = {"DATE", "INT"}, returnType = "DATE")
    public static Expression dateAdd(DateLiteral date, IntegerLiteral day) {
        return daysAdd(date, day);
    }

    @ExecFunction(name = "date_add", argTypes = {"DATETIME", "INT"}, returnType = "DATETIME")
    public static Expression dateAdd(DateTimeLiteral date, IntegerLiteral day) {
        return daysAdd(date, day);
    }

    @ExecFunction(name = "date_add", argTypes = {"DATEV2", "INT"}, returnType = "DATEV2")
    public static Expression dateAdd(DateV2Literal date, IntegerLiteral day) {
        return daysAdd(date, day);
    }

    @ExecFunction(name = "date_add", argTypes = {"DATETIMEV2", "INT"}, returnType = "DATETIMEV2")
    public static Expression dateAdd(DateTimeV2Literal date, IntegerLiteral day) {
        return daysAdd(date, day);
    }
    ...
}
```
上面计算正确的 DAY/MONTH/WEEK 都是在FE进行计算的。
```
DATE (3 bytes)
    Range: 0000-01-01 ~ 9999-12-31
DATETIME (8 bytes)
    Range: 0000-01-01 00:00:00 ~ 9999-12-31 23:59:59
```

```sql
CREATE TABLE IF NOT EXISTS demo.date_add
(
    `row_id` LARGEINT NOT NULL,
    `date` DATE NOT NULL,
    `date_null` DATE NULL,
    `dateV2` DATEV2 NOT NULL,
    `dateV2_null` DATEV2 NULL,
    `datetime` DATETIME NOT NULL,
    `datetime_null` DATETIME NULL,
)
DUPLICATE KEY(`row_id`)
DISTRIBUTED BY HASH(`row_id`) BUCKETS 1
PROPERTIES (
    "replication_allocation" = "tag.location.default: 1"
);

set enable_insert_strict=false;

INSERT INTO date_add VALUES (1, '0000-01-01', '0000-01-01', '0000-01-01', '0000-01-01', '0000-01-01 00:00:00', '0000-01-01 00:00:00');
INSERT INTO date_add VALUES (2, '0000-01-01', NULL, '0000-01-01', NULL, '0000-01-01 00:00:00', NULL);
INSERT INTO date_add VALUES (3, '9999-12-31', '9999-12-31', '9999-12-31', '9999-12-31', '9999-12-31 23:59:59', '9999-12-31 23:59:59');
INSERT INTO date_add VALUES (4, '9999-12-31', NULL, '9999-12-31', NULL, '9999-12-31 23:59:59', NULL);


select row_id,
       date_add(date, INTERVAL -1 YEAR), date_add(date_null, INTERVAL -1 YEAR),
       date_add(dateV2, INTERVAL -1 YEAR), date_add(dateV2_null, INTERVAL -1 YEAR)
from date_add WHERE row_id = 1 OR row_id = 2 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL -1 MONTH), date_add(date_null, INTERVAL -1 MONTH),
       date_add(dateV2, INTERVAL -1 MONTH), date_add(dateV2_null, INTERVAL -1 MONTH)
from date_add WHERE row_id = 1 OR row_id = 2 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL -1 WEEK), date_add(date_null, INTERVAL -1 WEEK),
       date_add(dateV2, INTERVAL -1 WEEK), date_add(dateV2_null, INTERVAL -1 WEEK)
from date_add WHERE row_id = 1 OR row_id = 2 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL -1 DAY), date_add(date_null, INTERVAL -1 DAY),
       date_add(dateV2, INTERVAL -1 DAY), date_add(dateV2_null, INTERVAL -1 DAY)
from date_add WHERE row_id = 1 OR row_id = 2 ORDER BY row_id;


select row_id,
       date_add(date, INTERVAL -1 MINUTE), date_add(date_null, INTERVAL -1 MINUTE),
       date_add(dateV2, INTERVAL -1 MINUTE), date_add(dateV2_null, INTERVAL -1 MINUTE)
from date_add WHERE row_id = 1 OR row_id = 2 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL -1 SECOND), date_add(date_null, INTERVAL -1 SECOND),
       date_add(dateV2, INTERVAL -1 SECOND), date_add(dateV2_null, INTERVAL -1 SECOND)
from date_add WHERE row_id = 1 OR row_id = 2 ORDER BY row_id;
-------------------
select row_id,
       date_add(date, INTERVAL 1 YEAR), date_add(date_null, INTERVAL 1 YEAR),
       date_add(dateV2, INTERVAL 1 YEAR), date_add(dateV2_null, INTERVAL 1 YEAR)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 1 MONTH), date_add(date_null, INTERVAL 1 MONTH),
       date_add(dateV2, INTERVAL 1 MONTH), date_add(dateV2_null, INTERVAL 1 MONTH)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 1 WEEK), date_add(date_null, INTERVAL 1 WEEK),
       date_add(dateV2, INTERVAL 1 WEEK), date_add(dateV2_null, INTERVAL 1 WEEK)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 1 DAY), date_add(date_null, INTERVAL 1 DAY),
       date_add(dateV2, INTERVAL 1 DAY), date_add(dateV2_null, INTERVAL 1 DAY)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 1 MINUTE), date_add(date_null, INTERVAL 1 HOUR),
       date_add(dateV2, INTERVAL 1 MINUTE), date_add(dateV2_null, INTERVAL 1 MINUTE)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 1 MINUTE), date_add(date_null, INTERVAL 1 MINUTE),
       date_add(dateV2, INTERVAL 1 MINUTE), date_add(dateV2_null, INTERVAL 1 MINUTE)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 1 SECOND), date_add(date_null, INTERVAL 1 SECOND),
       date_add(dateV2, INTERVAL 1 SECOND), date_add(dateV2_null, INTERVAL 1 SECOND)
from date_add WHERE row_id = 3 OR row_id = 4 ORDER BY row_id;


select row_id,
       date_add(date, INTERVAL 15900000 WEEK), date_add(date_null, INTERVAL 15900000 WEEK),
       date_add(dateV2, INTERVAL 15900000 WEEK), date_add(dateV2_null, INTERVAL 15900000 WEEK)
from date_add ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 15900000 DAY), date_add(date_null, INTERVAL 15900000 DAY),
       date_add(dateV2, INTERVAL 15900000 DAY), date_add(dateV2_null, INTERVAL 15900000 DAY)
from date_add ORDER BY row_id;

select row_id,
       date_add(date, INTERVAL 15900000 MINUTE), date_add(date_null, INTERVAL 15900000 MINUTE),
       date_add(dateV2, INTERVAL 15900000 MINUTE), date_add(dateV2_null, INTERVAL 15900000 MINUTE)
from date_add ORDER BY row_id;
```



#### mysql
DATE_ADD 函数的返回值取决于参数情况
`DATE_ADD(date,INTERVAL expr unit)`
* 如果 date 是 null，那么结果也是 null
* 如果