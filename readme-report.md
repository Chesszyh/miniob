# MiniOB 源码分析

Gemini Github Codebase功能上下文窗口依然有限，无法准确获取整个功能实现的全部流程(比如`CREATE TABLE`，到第3步时已经无法获取到后续的代码了)，需要在报告基础上手动补充缺失部分代码。

## CREATE TABLE 功能

`CREATE TABLE` 是数据库系统中用于定义新表结构的核心命令。在 MiniOB 中，这一功能的实现涉及多个模块，从 SQL 解析到最终的元数据持久化和存储结构创建。以下是对 MiniOB 中 `CREATE TABLE` 功能的简要分析：

### 1. SQL 解析与语句构建

当用户执行 `CREATE TABLE` 语句时，其处理流程大致如下：

* **词法与语法解析**:
    * 输入 SQL 语句（如 `CREATE TABLE my_table (id INT PRIMARY KEY, name VARCHAR(255)) STORE AS ROW;`）首先由解析器处理。
    * `parse.h` 和 `parse.cpp` 中的 `parse()` 函数接收原始 SQL 字符串。它内部调用（词法分析器，如 Flex ；和语法分析器，如 Bison）`sql_parse()` 函数将 SQL 文本转换为内部的解析树结构。
    * 对于 `CREATE TABLE` 语句，会生成一个 `CreateTableSqlNode` 对象（定义在 `parse_defs.h` 中）。这个节点包含了表名、列定义（名称、类型、长度、约束如 `PRIMARY KEY`）、存储格式等从 SQL 语句中提取的信息。

    ```cpp
    struct CreateTableSqlNode
    {
    string                  relation_name;  ///< Relation name
    vector<AttrInfoSqlNode> attr_infos;     ///< attributes
    // TODO: integrate to CreateTableOptions
    string storage_format;  ///< storage format
    string storage_engine;  ///< storage engine
    };
    ```

* **语句对象转换**:
    * `create_table_stmt.h` 和 `create_table_stmt.cpp` 定义了 `CreateTableStmt` 类，它是 `Stmt` 的子类，专门用于表示 `CREATE TABLE` 操作。
    * `CreateTableStmt::create()` 静态方法负责将解析阶段生成的 `CreateTableSqlNode` 转换为一个 `CreateTableStmt` 对象。此过程中，它会处理表名、列属性列表 (`attr_infos_`)、主键列表 (`primary_keys_`)。
    * `CreateTableStmt::get_storage_format()` 方法会解析用户指定的存储格式（如 "ROW" 或 "PAX"），并将其转换为内部的 `StorageFormat` 枚举类型。如果未指定，则默认为 `ROW_FORMAT`.

### 2. 语句执行

解析和构建完 `CreateTableStmt` 对象后，执行阶段开始：

* **执行器调用**:
    * `create_table_executor.h` 和 `create_table_executor.cpp` 中定义的 `CreateIndexExecutor` 负责执行 `CREATE TABLE` 语句。
    * 其核心方法 `CreateIndexExecutor::execute(SQLStageEvent *sql_event)` 从 `sql_event` 中获取 `CreateTableStmt` 对象和当前的会话 (`Session`) 信息。
    * 关键步骤是调用 `session->get_current_db()->create_table(...)`。这表明实际的表创建逻辑封装在当前数据库 (`Db`) 对象的 `create_table` 方法中。传递给此方法的参数包括表名、列信息、主键信息和存储格式。

### 3. 元数据管理与持久化

`Db::create_table()` 方法（其具体实现未在本次分析的文件中，但其接口和职责可以推断）会处理表的元数据创建和持久化：

* **表元数据定义 (`TableMeta`)**:
    * `table_meta.h` 和 `table_meta.cpp` 定义了 `TableMeta` 类，用于封装表的完整元数据。
    * `TableMeta` 包含 `table_id_`、`name_` (表名)、`trx_fields_` (事务相关系统字段元数据列表)、`fields_` (包含用户定义字段和事务相关系统字段的完整列元数据列表，类型为 `FieldMeta`)、`indexes_` (索引元数据列表)、`storage_format_`、`storage_engine_` 和 `record_size_` 属性。
    * `TableMeta::init()` 方法接收 `table_id`、表名 `name`、一个可选的指向 `vector<FieldMeta>` 的指针用于指定事务相关字段 (`trx_fields`)、一个 `span<const AttrInfoSqlNode>` 用于描述用户定义的列属性、存储格式 `storage_format` 和存储引擎 `storage_engine`。
        * 该方法首先处理事务相关字段（如果提供）：将它们复制到内部的 `trx_fields_` 成员，并添加到 `fields_` 列表的开头，同时标记这些字段为不可见 (`visible = false`)。
        * 接着，它遍历用户定义的列属性，为每个用户列初始化一个 `FieldMeta` 对象，并将其追加到 `fields_` 列表中，标记为可见 (`visible = true`)。
        * 在此过程中，会计算每个字段的偏移量 `field_offset`，最终的 `field_offset` 被用来设置记录的总大小 `record_size_`。
    * `TableMeta` 提供了 `serialize(ostream &ss)` 和 `deserialize(istream &is)` 方法，使用 JSON 格式进行序列化和反序列化。这意味着表的元数据可以被持久化到存储中（例如，存储在特定的元数据文件或系统表中）。在反序列化时，`trx_fields_` 成员会根据 `fields_` 中标记为不可见的字段重新填充。

* **实际创建过程**:
    * 在 `Db::create_table(const char *table_name, span<const AttrInfoSqlNode> attributes, const StorageFormat storage_format, const StorageEngine storage_engine)` 方法内部：
        * 首先会检查 `opened_tables_` 中是否已存在同名表，如果存在则返回 `RC::SCHEMA_TABLE_EXIST` 错误。
        * 若不存在，则会为新表分配一个 `table_id` (通过 `next_table_id_++` 实现)。
        * 接着，构造表元数据文件的路径（例如 `path_ + "/" + table_name + ".meta"`）。
        * 创建一个新的 `Table` 对象实例。
        * 调用 `Table::create()` 方法。这个方法是实际执行表创建的核心，它会接收 `Db` 对象指针、`table_id`、元数据文件路径、表名、数据库路径、列属性、存储格式和存储引擎作为参数。
            * 在 `Table::create()` 内部（根据其职责推断，并结合 `TableMeta` 的功能）：
                * 会创建一个 `TableMeta` 对象。
                * 调用 `TableMeta::init()` 方法，使用传入的列属性 (`attributes`)、`table_id`、表名、存储格式和引擎等信息来初始化元数据。
                * 调用 `TableMeta::serialize()` 将元数据序列化（例如为 JSON 格式）并写入到指定的元数据文件路径中，实现持久化。
                * 根据存储格式和引擎，初始化表的数据存储结构（例如，如果是堆表，可能会创建数据文件或分配初始页面）。
        * 如果 `Table::create()` 成功，则将新创建的 `Table` 对象添加到 `Db` 实例的 `opened_tables_` 映射中。

* **目录服务 (`Catalog`)**:
    * `catalog.h` 和 `catalog.cpp` 定义了 `Catalog` 类，它通过 `Catalog::get_instance()` 提供一个单例。
    * `Catalog` 内部使用一个 `unordered_map<int, TableStats> table_stats_` 来存储每个表（通过 `table_id` 索引）的统计信息 (`TableStats`)，例如行数、不同值的数量等。这些信息对查询优化器非常重要。
    * 提供了 `get_table_stats(int table_id)` 和 `update_table_stats(int table_id, const TableStats &table_stats)` 方法来访问和更新这些统计信息，并使用 `std::mutex` 进行并发控制。
    * 从代码注释和实现来看，`Catalog` 中的统计信息**当前不是持久化的**（"This map is currently not persisted and its persistence is planned via a system table."）。
    * 在 `Db::create_table()` 的核心流程中，**没有直接与 `Catalog` 交互的代码**。这意味着在表创建时，并不会立即在 `Catalog` 中创建或初始化统计信息条目。统计信息的填充和更新更可能是在后续的数据操作（如 `INSERT`, `DELETE`, `UPDATE`）或专门的统计信息收集命令（如 `ANALYZE TABLE`）执行后，通过调用 `Catalog::update_table_stats()` 来完成。

### 总结

MiniOB 的 `CREATE TABLE` 功能实现了一个清晰的分层流程：
1.  **解析层**：将 SQL 文本转换为结构化的 `CreateTableSqlNode`。
2.  **语句层**：将 `CreateTableSqlNode` 转换为更具体的 `CreateTableStmt` 对象，并处理存储格式等细节。
3.  **执行层**：`CreateIndexExecutor` 调度执行，并委托给 `Db` 对象。
4.  **存储与元数据层**：`Db` 对象负责与 `TableMeta` 交互，初始化并持久化表的元数据，并管理底层数据存储的创建。

这种模块化的设计使得各个组件职责分明，易于理解和扩展。持久化的元数据确保了数据库重启后表定义的恢复。