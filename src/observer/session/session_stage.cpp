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
// Created by Longda on 2021/4/13.
//

#include "session_stage.h"

#include <string.h>
#include <string>

#include "common/conf/ini.h"
#include "common/log/log.h"
#include "common/seda/timer_stage.h"

#include "common/lang/mutex.h"
#include "common/metrics/metrics_registry.h"
#include "common/seda/callback.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "net/server.h"
#include "net/communicator.h"
#include "session/session.h"

using namespace common;

const std::string SessionStage::SQL_METRIC_TAG = "SessionStage.sql";

// Constructor
SessionStage::SessionStage(const char *tag) : Stage(tag), query_cache_stage_(nullptr), sql_metric_(nullptr)
{}

// Destructor
SessionStage::~SessionStage()
{}

// Parse properties, instantiate a stage object
Stage *SessionStage::make_stage(const std::string &tag)
{
  SessionStage *stage = new (std::nothrow) SessionStage(tag.c_str());
  if (stage == nullptr) {
    LOG_ERROR("new ExecutorStage failed");
    return nullptr;
  }
  stage->set_properties();
  return stage;
}

// Set properties for this object set in stage specific properties
bool SessionStage::set_properties()
{
  //  std::string stageNameStr(stage_name_);
  //  std::map<std::string, std::string> section = g_properties()->get(
  //    stageNameStr);
  //
  //  std::map<std::string, std::string>::iterator it;
  //
  //  std::string key;

  return true;
}

// Initialize stage params and validate outputs
bool SessionStage::initialize()
{
  std::list<Stage *>::iterator stgp = next_stage_list_.begin();
  query_cache_stage_ = *(stgp++);

  MetricsRegistry &metricsRegistry = get_metrics_registry();
  sql_metric_ = new SimpleTimer();
  metricsRegistry.register_metric(SQL_METRIC_TAG, sql_metric_);
  return true;
}

// Cleanup after disconnection
void SessionStage::cleanup()
{
  MetricsRegistry &metricsRegistry = get_metrics_registry();
  if (sql_metric_ != nullptr) {
    metricsRegistry.unregister(SQL_METRIC_TAG);
    delete sql_metric_;
    sql_metric_ = nullptr;
  }
}

void SessionStage::handle_event(StageEvent *event)
{
  // right now, we just support only one event.
  handle_request(event);

  return;
}

void SessionStage::callback_event(StageEvent *event, CallbackContext *context)
{
  SessionEvent *sev = dynamic_cast<SessionEvent *>(event);
  if (nullptr == sev) {
    LOG_ERROR("Cannot cat event to sessionEvent");
    return;
  }

  Communicator *communicator = sev->get_communicator();
  bool need_disconnect = false;
  RC rc = communicator->write_result(sev, need_disconnect);
  LOG_INFO("write result return %s", strrc(rc));
  if (need_disconnect) {
    Server::close_connection(communicator);
  }
  Session::set_current_session(nullptr);

  return;
}

void SessionStage::handle_request(StageEvent *event)
{
  SessionEvent *sev = dynamic_cast<SessionEvent *>(event);
  if (nullptr == sev) {
    LOG_ERROR("Cannot cat event to sessionEvent");
    return;
  }

  TimerStat sql_stat(*sql_metric_);

  std::string sql = sev->query();
  if (common::is_blank(sql.c_str())) {
    sev->done_immediate();
    return;
  }

  CompletionCallback *cb = new (std::nothrow) CompletionCallback(this, nullptr);
  if (cb == nullptr) {
    LOG_ERROR("Failed to new callback for SessionEvent");
    sev->done_immediate();
    return;
  }

  sev->push_callback(cb);

  Session::set_current_session(sev->session());
  SQLStageEvent *sql_event = new SQLStageEvent(sev, sql);
  query_cache_stage_->handle_event(sql_event);
}
