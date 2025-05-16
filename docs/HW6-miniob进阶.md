# Miniob进阶

- Cursor 导入 Vscode 配置：https://github.com/maomao1996/daily-notes/issues/50
- Miniob提交：https://open.oceanbase.com/train/detail/3?questionId=200001
- [Using C++ and WSL in VS Code](https://code.visualstudio.com/docs/cpp/config-wsl)

注意，我的Miniob-Docker是部署在AWS上的，因为国内的Docker没法连到Github。

## 启动开发

当前开发流程：

- 本地：Docker已全部删除，直接在WSL下编译即可。
- 远程(AWS)：有miniob原生容器，可以用作对比测试。

本地使用Cursor+GDB调试：先启动Miniob(F5)，再进行连接：

```shell
cd build
./bin/obclient -h 127.0.0.1 -p 6789
```

测试命令：

```sql
create table test_drop (id int, name char(10));
insert into test_drop values (1, 'test1'), (2, 'test2'), (3, 'test3');
show tables;
drop table test_drop;
show tables;
```

### 杂项问题

我一周前clone的原生仓库，今天我先fork并连接到自己的分支，再在本地原生仓库修改，现在原仓库可能有一些改动，包含在了我的fork里。因此，直接`git pull`会失败。

**解决**：

```shell
# 0. 设置pull策略为rebase，保持提交记录的整洁
git config pull.rebase true
# 1. 添加你的远程仓库
git remote add origin https://github.com/Chesszyh/miniob.git
# 2. 从上游仓库拉取最新代码并变基
git pull upstream main --rebase
```

输出：有一处文件冲突，一个新增文件夹冲突，其他文件都auto-merged。

```shell
From https://github.com/oceanbase/miniob
 * branch            main       -> FETCH_HEAD
warning: unable to rmdir 'docs/lectures-on-dbms-implementation': Directory not empty
Auto-merging src/common/sys/rc.h
CONFLICT (content): Merge conflict in src/common/sys/rc.h
Auto-merging src/observer/sql/parser/parse_defs.h
Auto-merging src/observer/storage/db/db.cpp
Auto-merging src/observer/storage/db/db.h
Auto-merging src/observer/storage/default/default_handler.cpp
Auto-merging src/observer/storage/table/table.cpp
Auto-merging src/observer/storage/table/table.h
error: could not apply 673c550... add 'drop table` feature
hint: Resolve all conflicts manually, mark them as resolved with
hint: "git add/rm <conflicted_files>", then run "git rebase --continue".
hint: You can instead skip this commit: run "git rebase --skip".
hint: To abort and get back to the state before "git rebase", run "git rebase --abort".
Could not apply 673c550... add 'drop table` feature
```

解决`rc.h`文件冲突，然后：

```shell
# 3. 推送到你的远程仓库
git add .
git rebase --continue
git push origin main
```

这样操作可以：

- 保持提交历史整洁
- 确保代码基于最新的上游代码
- 避免产生不必要的合并提交

如果之后想避免这个问题，建议：

- 经常从上游仓库同步代码
- 在开发新功能时创建新的分支
- 完成功能后再合并到主分支

## 调试

数据库启动后，会在`one_thread_per_connection_thread_handler.cpp`的81行进行无限循环，等待用户输入sql命令。

### Create Table



### Drop Table

- 实现删除表(drop table)，清除表相关的资源。
- 当前MiniOB支持建表与创建索引，但是没有删除表的功能。
- 在实现此功能时，除了要删除所有与表关联的数据，不仅包括磁盘中的文件，还包括内存中的索引等数据。
- 删除表的语句为 `drop table table-name`

#### 执行流程

1. `net/one_thread_per_connection_thread_handler.cpp`：进入`net/sql_task_handler.cpp`的`handle_event`函数；
2. `handle_event`函数：调用`net/plain_communicator.cpp`的`read_event`函数，通过网络通信解析并设置原始SQL语句；然后，调用`observer/session/session_stage.cpp`的`handle_request2`函数，设置当前session；
3. 继续在`sql_task_handler.cpp`里执行，执行到`rc = handle_sql`，调用本文件内的`handle_sql`函数；`handle_sql`分多步执行`handle_request`：缓存检查、解析、语义解析、执行、返回结果；
    1. `query_cache_stage`：调用`query_cache_stage.cpp`的`handle_request`函数，该函数直接返回`RC::SUCCESS`，推测是“检查是否有缓存的查询结果”(标了一个FIX)；
    2. `parse_stage_`：调用`observer/sql/parser/parse_stage.cpp`的`handle_request`函数，进行：
        1. SQL词法分析：将`drop table test`解析为`DROP`、`TABLE`、`test`三个token；
        2. SQL语法解析：构建抽象语法树AST
        3. 识别为 `DROP TABLE` 操作，创建并设置sql node：`DropTableSqlNode`，包含表名`test`；
        4. 返回`RC::SUCCESS`；
    3. `resolve_stage_`：调用`observer/sql/resolve_stage.cpp`的`handle_request`函数，进行SQL语义解析，`resolve_stage.cpp`需要调用`observer/sql/stmt/stmt.cpp`的`create_stmt`函数；
        1. `create_stmt`函数：命中`SCF_DROP_TABLE`case，调用我新建的`observer/sql/stmt/drop_table_stmt.cpp`的`create`函数；
    4. `optimize_stage_`：
    5. `execute_stage_`：
4. 执行完以上5步后，返回`handle_event`函数，调用`net/plain_communicator.cpp`的`write_result`写入结果，并检查是否需要关闭连接，需要则返回`RC::INTERNAL`，否则返回`RC::SUCCESS`；
5. `handle_event`函数返回，重新进入`one_thread_per_connection_thread_handler.cpp`的循环，等待下次输入。

#### 实现

？

1. `drop_table_stmt.cpp`：实现`DropTableStmt`类，包含表名`test`的成员变量。
2. `drop_table_stmt.cpp`：实现`DropTableStmt::create`函数，返回一个新的`DropTableStmt`对象。
3. `drop_table_stmt.cpp`：实现`DropTableStmt::execute`函数，执行删除表的操作，包括：
   1. 从元数据中删除表信息。
   2. 删除表相关的磁盘文件。
   3. 清理内存中的索引等数据。

### Select Table

当前系统支持单表查询的功能，需要在此基础上支持多张表的笛卡尔积关联查询。

需要实现

select * from t1,t2; 

select t1.,t2. from t1,t2;

select t1.id,t2.id from t1,t2;

查询可能会带条件。查询结果展示格式参考单表查询。注意查询条件中的“不等”比较，除了"<>"还要考虑"!=" 比较符号。每一列必须带有表信息，比如:

t1.id | t2.id

1 | 1

### Date

在现有功能上实现日期类型字段。
当前已经支持了int、char、float类型，在此基础上实现date类型的字段。date测试可能超过2038年2月，也可能小于1970年1月1号。注意处理非法的date输入（考虑date 类型的值的合法性，如考虑闰年的情况），需要返回FAILURE。
这道题目需要考虑语法解析，类型相关操作，还需要考虑DATE类型数据的存储。

### Update

实现更新行数据的功能。
当前实现update单个字段即可。现在MiniOB具有insert和delete功能，在此基础上实现更新功能。可以参考insert_record和delete_record的实现。目前仅能支持单字段update的语法解析，但是不能执行。需要考虑带条件查询的更新，和不带条件的更新，同时需要考虑带索引时的更新。

