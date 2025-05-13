# Miniob进阶

- Cursor 导入 Vscode 配置：https://github.com/maomao1996/daily-notes/issues/50

## Setup

```shell
# You're in WSL's miniob directory
# --privileged: 获取root权限
# -v: 挂载本地目录到容器内(共享目录，修改实时同步)
docker run -itd --name miniob-test --privileged -v /root/Project/miniob:/root/miniob oceanbase/miniob
```

当前，`miniob`容器是官方原生容器，`miniob-test`是我进行开发的容器，WSL下的环境实际上应该是不需要的(如果不是直接在WSL下编译的话)。

## Drop Table

- 实现删除表(drop table)，清除表相关的资源。
- 当前MiniOB支持建表与创建索引，但是没有删除表的功能。
- 在实现此功能时，除了要删除所有与表关联的数据，不仅包括磁盘中的文件，还包括内存中的索引等数据。
- 删除表的语句为 `drop table table-name`

## Update

实现更新行数据的功能。
当前实现update单个字段即可。现在MiniOB具有insert和delete功能，在此基础上实现更新功能。可以参考insert_record和delete_record的实现。目前仅能支持单字段update的语法解析，但是不能执行。需要考虑带条件查询的更新，和不带条件的更新，同时需要考虑带索引时的更新。