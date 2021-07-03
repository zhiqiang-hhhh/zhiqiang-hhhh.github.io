# Clickhouse权限管理

## 整体逻辑
### 参数策略
可以在`users.xml`中的`profile`标签内创建多种不同的参数策略。`default`是默认使用的参数策略，必须被配置。

```xml
<profile>
	<default>
		<max_memory_usage>322122547200<max_memory_usage>
        <load_balancing>in_order</load_balancing>
	</default>
	<test1>
		...
	</test1>
	<test2>
		<!--可以继承-->
		<profile>test1</profile>  
		...
	</test2>
</profile>
```
### 参数限制
通过`constraints`标签限制哪些参数可以通过Query进行修改，以及可以被设置的范围。
### 用户定义
注意，前面设置的都是参数管理策略，不涉及到用户名，以及访问控制。用户定义需要在`users`标签下进行。
```xml
<users>
    <username>hzq<username>
    <!-- 免密登陆 -->
    <password></password> 
</users>
```
设置加密密码
```bash
echo -n 123 | openssl dgst -sha256
a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3
```
使用`password_sha256_hex`标签。

`<password_sha256_hex>a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3</password_sha256_hex>`



