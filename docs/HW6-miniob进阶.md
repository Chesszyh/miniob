# Miniob进阶

- Cursor 导入 Vscode 配置：https://github.com/maomao1996/daily-notes/issues/50
- Miniob提交：https://open.oceanbase.com/train/detail/3?questionId=200001
    - 注意，提测会测试所有题目
- [Using C++ and WSL in VS Code](https://code.visualstudio.com/docs/cpp/config-wsl)

注意，我的Miniob-Docker是部署在AWS上的，因为国内的Docker没法连到Github。

## 启动开发

当前开发流程：

- 本地：Docker已全部删除，直接在WSL下编译。
- 远程(AWS)：有miniob原生容器(main+2024competition)，可以用作对比测试。main和2024competition的代码差异不大，仅相差大概50个commit，实际测试时在`SELECT`和`DROP`上基本也没有差异。

本地使用Cursor/Vscode+GDB调试：先启动Miniob(F5)，再进行连接：

```shell
cd build
./bin/obclient -h 127.0.0.1 -p 6789
```

**注意**：

1. 我的GDB似乎不支持热重载，必须重新编译启动才能应用更改；

2. **每次F5应在同一个活动窗口下**，因为miniob会在与你活动窗口同级的文件夹下创建一个新的`miniob`文件夹(存放`data`,`table`,`log`等)，每次F5的位置都不一样的话，miniob就会在你的项目里到处拉屎，每次新开一个调试进程就会创建一个新数据库。

**测试命令**：

### Drop Table

```sql
-- Drop Table
show tables;
create table test_drop (id int, name char(10));
insert into test_drop values (1, 'test1'), (2, 'test2'), (3, 'test3');
-- Drop Table
drop table test_drop;
show tables;
select * from test_drop;    -- 应输出FAILURE
```

### Select Table

```sql
-- Select Table
-- 创建第一张表
create table t1 (id int, name char(10));
insert into t1 values (1, 'Alice'), (2, 'Bob'), (3, 'Charlie');

-- 创建第二张表
create table t2 (id int, score int);
insert into t2 values (1, 90), (2, 85), (4, 95);
```

**测试**：

```sql
-- 多表查询测试
-- 全表笛卡尔积，未通过
select * from t1, t2;
-- 结果应该有9行(3×3)，每行包含t1和t2的所有列
-- 实际仅1行：1 | Alice | 1 | 90

-- 指定表的所有列，未通过
select t1.*, t2.* from t1, t2;
-- 结果应与上一查询相同
-- 实际：Failed to parse sql

-- 指定具体列
select t1.id, t1.name, t2.score from t1, t2;
-- 只显示指定的3列，此测试已通过

-- 带条件的多表查询
select t1.id, t1.name, t2.score from t1, t2 where t1.id = t2.id;
-- 只返回id匹配的行(相当于内连接)，此测试已通过

-- 不等条件查询(使用!=)，未通过
select t1.id, t1.name, t2.id, t2.score from t1, t2 where t1.id != t2.id;

-- 不等条件查询(使用<>)，未通过
select t1.id, t1.name, t2.id, t2.score from t1, t2 where t1.id <> t2.id;

-- 复杂条件查询，未通过
select t1.id, t2.score from t1, t2 where t1.id < t2.id and t2.score > 90;
```

## 调试

数据库启动后，会在`one_thread_per_connection_thread_handler.cpp`的81行进行无限循环，等待用户输入sql命令。

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
    4. `optimize_stage_`：调用`observer/sql/optimizer/optimize_stage.cpp`的`handle_request`函数，该函数内调用`create_logical_plan`函数，创建逻辑计划；
        1. `create_logical_plan`函数，调用`LogicalPlanGenerator`类的`create`方法，`DROP TABLE`会命中`StmtType::DROP_TABLE` case，创建一个`DropTablePlan`对象；
            1. 可能返回`RC::SUCCESS`，也可能返回`RC::UNIMPLEMENT`，表示当前操作不支持/未实现，但`sql_task_handler.cpp`进行检查时，会认为`rc != RC::UNIMPLEMENTED && rc != RC::SUCCESS`时都算成功；
            2. `DROP TABLE`不算复杂操作，如果为了架构一致性和可扩展性，可以创建具体的逻辑计划。这次作业实现我选择直接在`LogicalPlanGenerator.create`里返回`RC::UNIMPLEMENTED`，然后交给`execute_stage_`处理。
        3. `DROP TABLE`在`create_logical_plan`后即返回，后续的`optimize`、`generate_physical_plan`等优化不再执行(但`SELECT`等复杂查询会执行)。
        
    5. `execute_stage_`：调用`observer/sql/executor/execute_stage.cpp`的`handle_request`函数，进行SQL执行；
        1. `handle_request`：SQL命令可能有两种处理方式：
            1. 物理算子：通常是针对 `SELECT、JOIN` 等需要优化的复杂查询操作，包含了执行查询的详细步骤（如表扫描、索引扫描、连接算法等），其路径应用了查询优化器的**优化结果**
                1. 物理算子会生成**执行计划**，走`ExecuteStage`类的`handle_request_with_physical_operator`函数；
            2. 逻辑算子：通常是针对 `CREATE、INSERT、UPDATE、DELETE` 等非DQL语句，走`CommandExecutor`类的`execuate`函数直接执行即可。
        2. `DROP TABLE`命令属于逻辑算子，走我新建的case`StmtType::DROP_TABLE`，调用新建的`observer/sql/executor/drop_table_executor.cpp`的`DropTableExecutor`类的`execute`函数，执行具体的删除表操作。参考：`src/observer/sql/executor/drop_table_executor.cpp`实现
        
4. 执行完以上5步后，返回`handle_event`函数，调用`net/plain_communicator.cpp`的`write_result`写入结果，并检查是否需要关闭连接，需要则返回`RC::INTERNAL`，否则返回`RC::SUCCESS`；
5. `handle_event`函数返回，重新进入`one_thread_per_connection_thread_handler.cpp`的循环，等待下次输入。

### Select Table

当前系统支持单表查询的功能，需要在此基础上支持多张表的笛卡尔积关联查询。

**仅支持**：`select * from t1;`

**需要实现**：

`select * from t1,t2; `

`select t1.,t2. from t1,t2;`

`select t1.id,t2.id from t1,t2;`

查询可能会带条件。查询结果展示格式参考单表查询。注意查询条件中的“不等”比较，除了"<>"还要考虑"!=" 比较符号。

#### 执行流程

流程基本与`drop table`相同，主要区别在于：

1. `observer/sql/stmt/select_stmt.cpp`的`create`静态方法：将解析树节点(SelectSqlNode)转换为语句对象(SelectStmt)。`SELECT`的语法比`DROP`复杂很多，所以处理流程也更复杂。

`SelectStmt`类负责：

- 组织FROM子句中的表
- 收集查询需要的字段表达式
- 处理GROUP BY子句的分组表达式
- 管理WHERE子句的过滤条件
- 构建最终的`SelectStmt`对象

2. `optimize_stage_`：调用`observer/sql/optimizer/optimize_stage.cpp`的`handle_request`函数，创建完逻辑计划之后，会继续调用`gererate_physical_plan`生成物理计划。当前官方main分支的代码在此处有许多TODO，包括`unify the RBO and CBO`、`better way to generate general child`、`error handle`等。

3. `execute_stage_`：与`DROP TABLE`不同，`SELECT`命令属于物理算子，走`ExecuteStage`类的`handle_request_with_physical_operator`函数。该函数会创建一个`PhysicalOperator`对象，负责执行查询操作。

## 杂项问题

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