/** @file

  traffic_ctl

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "traffic_ctl.h"
#include <time.h>
#include <I_RecDefs.h>

// Record data type names, indexed by TSRecordT.
static const char *
rec_typeof(int rec_type) {
  switch (rec_type) {
  case TS_REC_INT: return "INT";
  case TS_REC_COUNTER: return "COUNTER";
  case TS_REC_FLOAT: return "FLOAT";
  case TS_REC_STRING: return "STRING";
  case TS_REC_UNDEFINED: /* fallthru */
  default: return "UNDEFINED";
  }
}

// Record type name, indexed by RecT.
static const char *
rec_classof(int rec_class) {
  switch (rec_class) {
  case RECT_CONFIG: return "standard config";
  case RECT_LOCAL:  return "local config";
  case RECT_PROCESS:return "process metric";
  case RECT_NODE:   return "node metric";
  case RECT_CLUSTER:return "cluster metric";
  case RECT_PLUGIN: return "plugin metric";
  default: return "undefined";
  }
}

// Record access control, indexed by RecAccessT.
static const char *
rec_accessof(int rec_access)
{
  switch (rec_access) {
  case RECA_NO_ACCESS: return "no access";
  case RECA_READ_ONLY: return "read only";
  case RECA_NULL: /* fallthru */
  default: return "default";
  }
}

// Record access control, indexed by RecUpdateT.
static const char *
rec_updateof(int rec_updatetype)
{
  switch (rec_updatetype) {
  case RECU_DYNAMIC: return "dynamic, no restart";
  case RECU_RESTART_TS: return "static, restart traffic_server";
  case RECU_RESTART_TM: return "static, restart traffic_manager";
  case RECU_RESTART_TC: return "static, full restart";
  case RECU_NULL: /* fallthru */
  default: return "none";
  }
}

// Record check type, indexed by RecCheckT.
static const char *
rec_checkof(int rec_checktype)
{
  switch (rec_checktype) {
  case RECC_STR: return "string matching a regular expression";
  case RECC_INT: return "integer with a specified range";
  case RECC_IP: return "IP address";
  case RECC_NULL: /* fallthru */
  default: return "none";
  }
}

static std::string
timestr(time_t tm)
{
  char buf[32];
  return std::string(ctime_r(&tm, buf));
}

static void
format_record(const CtrlMgmtRecord& record, bool recfmt)
{
  CtrlMgmtRecordValue value(record);

  if (recfmt) {
    // XXX Detect CONFIG or LOCAL ...
    printf("CONFIG %s %s %s\n", record.name(), rec_typeof(record.type()), value.c_str());
  } else {
    printf("%s: %s\n", record.name(), value.c_str());
  }
}

static int
config_get(unsigned argc, const char ** argv)
{
  int recfmt = 0;
  const ArgumentDescription opts[] = {
    { "records", '-', "Emit output in records.config format", "F", &recfmt, NULL, NULL },
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments < 1) {
    return CtrlCommandUsage("config get [OPTIONS] RECORD [RECORD ...]", opts, countof(opts));
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecord record;
    TSMgmtError error;

    error = record.fetch(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    format_record(record, recfmt);
  }

  return CTRL_EX_OK;
}

static int
config_describe(unsigned argc, const char ** argv)
{
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments < 1) {
    return CtrlCommandUsage("config describe RECORD [RECORD ...]");
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    TSConfigRecordDescription desc;
    TSMgmtError error;

    ink_zero(desc);
    error = TSConfigRecordDescribe(file_arguments[i], 0 /* flags */, &desc);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to describe %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    printf("%-16s: %s\n", "Name", desc.rec_name);
    printf("%-16s: %s\n", "Current Value", CtrlMgmtRecordValue(desc.rec_type, desc.rec_value).c_str());
    printf("%-16s: %s\n", "Default Value", CtrlMgmtRecordValue(desc.rec_type, desc.rec_default).c_str());
    printf("%-16s: %s\n", "Record Type", rec_classof(desc.rec_class));
    printf("%-16s: %s\n", "Data Type", rec_typeof(desc.rec_type));
    printf("%-16s: %s\n", "Access Control ", rec_accessof(desc.rec_access));
    printf("%-16s: %s\n", "Update Type", rec_updateof(desc.rec_updatetype));
    printf("%-16s: 0x%" PRIx64 "\n", "Update Status", desc.rec_update);

    if (strlen(desc.rec_checkexpr)) {
      printf("%-16s: %s, '%s'\n", "Syntax Check", rec_checkof(desc.rec_checktype), desc.rec_checkexpr);
    } else {
      printf("%-16s: %s\n", "Syntax Check", rec_checkof(desc.rec_checktype));
    }

    printf("%-16s: %" PRId64 "\n", "Version", desc.rec_version);
    printf("%-16s: %" PRId64 "\n", "Order", desc.rec_order);
    printf("%-16s: %" PRId64 "\n", "Raw Stat Block", desc.rec_rsb);

    TSConfigRecordDescriptionFree(&desc);
  }

  return CTRL_EX_OK;
}

static int
config_set(unsigned argc, const char ** argv)
{
  TSMgmtError error;
  TSActionNeedT action;

  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 2) {
    return CtrlCommandUsage("config set RECORD VALUE");
  }

  error = TSRecordSet(file_arguments[0], file_arguments[1], &action);
  if (error  != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to set %s", file_arguments[0]);
    return CTRL_EX_ERROR;
  }

  switch (action) {
  case TS_ACTION_SHUTDOWN:
    printf("set %s, full shutdown required\n", file_arguments[0]);
    break;
  case TS_ACTION_RESTART:
    printf("set %s, restart required\n", file_arguments[0]);
    break;
  case TS_ACTION_RECONFIGURE:
    // printf("Set %s, reconfiguration required\n", file_arguments[0]);
    break;
  case TS_ACTION_DYNAMIC:
  default:
    printf("set %s\n", file_arguments[0]);
    break;
  }

  return CTRL_EX_OK;
}

static int
config_match(unsigned argc, const char ** argv)
{
  int recfmt = 0;
  const ArgumentDescription opts[] = {
    { "records", '-', "Emit output in records.config format", "F", &recfmt, NULL, NULL },
  };

  if (!CtrlProcessArguments(argc, argv, opts, countof(opts)) || n_file_arguments < 1) {
    return CtrlCommandUsage("config match [OPTIONS] REGEX [REGEX ...]", opts, countof(opts));
  }

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    CtrlMgmtRecordList reclist;
    TSMgmtError error;

    // XXX filter the results to only match configuration records.

    error = reclist.match(file_arguments[i]);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", file_arguments[i]);
      return CTRL_EX_ERROR;
    }

    while (!reclist.empty()) {
      CtrlMgmtRecord record(reclist.next());
      format_record(record, recfmt);
    }
  }

  return CTRL_EX_OK;
}

static int
config_reload(unsigned argc, const char ** argv)
{
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("config reload");
  }

  TSMgmtError error = TSReconfigure();
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "configuration reload request failed");
    return CTRL_EX_ERROR;
  }

  return CTRL_EX_OK;
}

static int
config_status(unsigned argc, const char ** argv)
{
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments != 0) {
    return CtrlCommandUsage("config status");
  }

  CtrlMgmtRecord version;
  CtrlMgmtRecord configtime;
  CtrlMgmtRecord starttime;
  CtrlMgmtRecord reconfig;
  CtrlMgmtRecord proxy;
  CtrlMgmtRecord manager;
  CtrlMgmtRecord cop;

  CTRL_MGMT_CHECK(version.fetch("proxy.process.version.server.long"));
  CTRL_MGMT_CHECK(starttime.fetch("proxy.node.restarts.proxy.start_time"));
  CTRL_MGMT_CHECK(configtime.fetch("proxy.node.config.reconfigure_time"));
  CTRL_MGMT_CHECK(reconfig.fetch("proxy.node.config.reconfigure_required"));
  CTRL_MGMT_CHECK(proxy.fetch("proxy.node.config.restart_required.proxy"));
  CTRL_MGMT_CHECK(manager.fetch("proxy.node.config.restart_required.manager"));
  CTRL_MGMT_CHECK(cop.fetch("proxy.node.config.restart_required.cop"));

  printf("%s\n", CtrlMgmtRecordValue(version).c_str());
  printf("Started at %s", timestr((time_t)starttime.as_int()).c_str());
  printf("Last reconfiguration at %s", timestr((time_t)configtime.as_int()).c_str());
  printf("%s\n", reconfig.as_int() ? "Reconfiguration required" : "Configuration is current");

  if (proxy.as_int()) {
    printf("traffic_server requires restarting\n");
  }
  if (manager.as_int()) {
    printf("traffic_manager requires restarting\n");
  }
  if (cop.as_int()) {
    printf("traffic_cop requires restarting\n");
  }

  return CTRL_EX_OK;
}

int
subcommand_config(unsigned argc, const char ** argv)
{
  const subcommand commands[] =
  {
    { config_describe, "describe", "Show detailed information about configuration values" },
    { config_get, "get", "Get one or more configuration values" },
    { config_match, "match", "Get configuration matching a regular expression" },
    { config_reload, "reload", "Request a configuration reload" },
    { config_set, "set", "Set a configuration value" },
    { config_status, "status", "Check the configuration status" },
  };

  return CtrlGenericSubcommand("config", commands, countof(commands), argc, argv);
}
