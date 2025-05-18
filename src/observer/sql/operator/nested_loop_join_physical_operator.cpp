/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by WangYunlai on 2022/12/30.
//

#include "sql/operator/nested_loop_join_physical_operator.h"

NestedLoopJoinPhysicalOperator::NestedLoopJoinPhysicalOperator() {}

RC NestedLoopJoinPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 2) {
    LOG_WARN("nlj operator should have 2 children");
    return RC::INTERNAL;
  }

  RC rc         = RC::SUCCESS;
  left_         = children_[0].get();
  right_        = children_[1].get();
  right_closed_ = true;
  round_done_   = true;

  rc   = left_->open(trx);
  trx_ = trx;
  return rc;
}

/**
    @brief: 1. open left and right child
            2. get the first tuple from left child
            3. get the first tuple from right child
            4. set the joined tuple
    @return RC::SUCCESS if success, RC::RECORD_EOF if failed

    @bug: 当前方法中的循环结构似乎是在内部消耗掉所有匹配的行对，或者过早退出，而不是一次产生一个连接后的行。
    @note:  如果对于当前左表的行，右表的扫描已经完成（或者这是左表当前行的第一次扫描），则推进左表。如果左表已经没有数据，则连接操作完成（返回 RC::RECORD_EOF）。
            如果从左表获取了一个新的行，则重置右表的扫描（重新打开它）。
            推进右表。
            如果在右表中找到一行，则形成了一个连接对。设置 joined_tuple_ 并返回 RC::SUCCESS。
            如果对于当前左表的行，右表已经没有数据，则标记它（round_done_ = true），然后循环回到步骤 1 以推进左表。
**/
// RC NestedLoopJoinPhysicalOperator::next()
// {
//   RC   rc             = RC::SUCCESS;
//   while (RC::SUCCESS == rc) {
//     bool left_need_step = (left_tuple_ == nullptr);
//     if (round_done_) {
//       left_need_step = true;
//     }

//     if (left_need_step) {
//       rc = left_next();
//       if (rc != RC::SUCCESS) {
//         return rc;
//       }
//     }

//     rc = right_next();
//     if (rc != RC::SUCCESS) {
//       if (rc == RC::RECORD_EOF) {
//         rc = RC::SUCCESS;
//         round_done_ = true;
//         continue;
//       } else {
//         return rc;
//       }
//     }
//   }
//   return rc;
// }

RC NestedLoopJoinPhysicalOperator::next() {
    RC rc;
    while (true) { // 循环直到找到一个连接元组或到达文件末尾
        // 推进左子节点的条件:
        // 1. 还没有左元组 (首次调用，或者在左子节点耗尽并通过调用者的 open() 隐式重置后)
        // 2. 当前 left_tuple_ 已经耗尽了所有 right_tuple_ (round_done_ 为 true)
        if (left_tuple_ == nullptr || round_done_) {
            rc = left_->next(); // 推进左子节点
            if (rc == RC::RECORD_EOF) {
                return RC::RECORD_EOF; // 左子节点已耗尽，连接完成
            } else if (OB_FAIL(rc)) {
                LOG_WARN("从左子节点获取下一条记录失败. rc=%s", strrc(rc));
                return rc; // 传播错误
            }
            left_tuple_ = left_->current_tuple();
            joined_tuple_.set_left(left_tuple_);

            // 新的左元组，因此重置右子节点
            if (!right_closed_) {
                rc = right_->close();
                if (OB_FAIL(rc)) {
                    LOG_WARN("重置时关闭右子节点失败. rc=%s", strrc(rc));
                    // 根据期望的稳健性，可能记录日志并继续，或返回错误
                }
                right_closed_ = true;
            }
            rc = right_->open(trx_);
            if (OB_FAIL(rc)) {
                LOG_WARN("重置时打开右子节点失败. rc=%s", strrc(rc));
                return rc;
            }
            right_closed_ = false;
            round_done_ = false; // 准备好对右子节点进行新一轮扫描
        }

        // 尝试从右子节点获取下一个元组
        rc = right_->next();
        if (rc == RC::SUCCESS) {
            right_tuple_ = right_->current_tuple();
            joined_tuple_.set_right(right_tuple_);
            // LOG_DEBUG("NLJ 生成: %s | %s", left_tuple_->to_string().c_str(), right_tuple_->to_string().c_str()); // 用于调试
            return RC::SUCCESS; // 成功找到一个连接对
        } else if (rc == RC::RECORD_EOF) {
            round_done_ = true; // 当前 left_tuple_ 的右子节点已耗尽。循环以推进左子节点。
                                // while(true) 循环的下一次迭代将处理推进左子节点。
        } else {
            LOG_WARN("从右子节点获取数据时出错. rc=%s", strrc(rc));
            return rc; // 右子节点的其他错误
        }
    }
}

RC NestedLoopJoinPhysicalOperator::close()
{
  RC rc = left_->close();
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to close left oper. rc=%s", strrc(rc));
  }

  if (!right_closed_) {
    rc = right_->close();
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to close right oper. rc=%s", strrc(rc));
    } else {
      right_closed_ = true;
    }
  }
  return rc;
}

Tuple *NestedLoopJoinPhysicalOperator::current_tuple() { return &joined_tuple_; }

RC NestedLoopJoinPhysicalOperator::left_next()
{
  RC rc = RC::SUCCESS;
  rc    = left_->next();
  if (rc != RC::SUCCESS) {
    return rc;
  }

  left_tuple_ = left_->current_tuple();
  joined_tuple_.set_left(left_tuple_);
  return rc;
}

RC NestedLoopJoinPhysicalOperator::right_next()
{
  RC rc = RC::SUCCESS;
  if (round_done_) {
    if (!right_closed_) {
      rc = right_->close();

      right_closed_ = true;
      if (rc != RC::SUCCESS) {
        return rc;
      }
    }

    rc = right_->open(trx_);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    right_closed_ = false;

    round_done_ = false;
  }

  rc = right_->next();
  if (rc != RC::SUCCESS) {
    if (rc == RC::RECORD_EOF) {
      round_done_ = true;
    }
    return rc;
  }

  right_tuple_ = right_->current_tuple();
  joined_tuple_.set_right(right_tuple_);
  return rc;
}
