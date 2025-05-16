// Added by Chesszyh on 2025/05/16

#include "sql/executor/drop_table_executor.h"

#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/drop_table_stmt.h"
#include "storage/db/db.h"

// Ref: create_table_executor.cpp

RC DropTableExecutor::execute(SQLStageEvent *sql_event)
{
  // 1. Get the session and statement from the sql_event
  // 2. Check if the statement is a DropTableStmt
  // 3. Get the table name from the statement
  // 4. Call the session's current database to drop the table
  // 5. Return the result code
  Stmt *stmt = sql_event->stmt();
  Session *session = sql_event->session_event()->session();
  ASSERT(stmt->type() == StmtType::DROP_TABLE,
        "drop table executor can not run this command: %d",
        static_cast<int>(stmt->type()));
  DropTableStmt *drop_table_stmt = static_cast<DropTableStmt *>(stmt);
  const char    *table_name      = drop_table_stmt->table_name().c_str();
  RC             rc              = session->get_current_db()->drop_table(table_name);
    if (rc != RC::SUCCESS) {
        LOG_ERROR("Failed to drop table %s", table_name);
        return rc;
    }
    LOG_INFO("Successfully dropped table %s", table_name);
    return rc;
}