## Date & Time in mysql

|-|DATE|DATETIME|TIMESTAMP|TIME|
|---|--|--|--|--|
|Format|YYYY-MM-DD|YYYY-MM-DD hh:mm:ss|YYYY-MM-DD hh:mm:ss|hh:mm:ss|
|Min|1000-01-01|1000-01-01 00:00:00|1970-01-01 00:00:01|-838:59:59|
|Max|9999-12-31|9999-12-31 23:59:59|2038-01-19 03:14:07|838:59:59|

* DATETIME & TIMESTAMP 都支持最高6位小数的秒部分，目前doris并不支持秒的小数部分所以这里不做对比。
