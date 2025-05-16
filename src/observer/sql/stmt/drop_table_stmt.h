// Added by Chesszyh on 2025/05/16
#pragma once

#include "sql/stmt/stmt.h"

class DropTableStmt : public Stmt
{
public:
  DropTableStmt(const std::string &table_name) : table_name_(table_name) {}
  virtual ~DropTableStmt() = default;

  StmtType type() const override { return StmtType::DROP_TABLE; }
  const std::string &table_name() const { return table_name_; }

  static RC create(Db *db, const DropTableSqlNode &drop_table, Stmt *&stmt);    // Ref: `parse_defs.h`

private:
  std::string table_name_;
};