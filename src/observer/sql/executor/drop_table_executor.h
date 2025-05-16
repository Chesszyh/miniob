// Added by Chesszyh on 2025/05/16
#pragma once

#include "common/sys/rc.h"

class SQLStageEvent;

class DropTableExecutor
{
public:
    DropTableExecutor() = default;
    virtual ~DropTableExecutor() = default;

    RC execute(SQLStageEvent *sql_event);
};