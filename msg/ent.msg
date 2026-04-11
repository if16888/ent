# ============================================================================
# ENT message specification
# ============================================================================
#
# Purpose
#   Maintain ENT return-code definitions in one readable text file.
#
# File format
#   module    <MODULE_NAME>    <MODULE_ID>
#   submodule <SUBMODULE_NAME> <SUBMODULE_ID>
#   <MSG_NAME> <ok|err> <INNER_ID> <message text...>
#
# Rules
#   1. MODULE_NAME and SUBMODULE_NAME must be uppercase and <= 4 chars.
#   2. MSG_NAME uses uppercase snake case and should stay descriptive.
#   3. err => generated value is negative, ok => generated value is non-negative.
#   4. submodule SYS / inner 0 should map to value 0 for compatibility.
#   5. Message text may contain printf-style placeholders such as %s %d %u.
#
# Editing tips
#   1. Keep one message per line.
#   2. Keep INNER_ID unique inside the same severity + submodule.
#   3. Add new messages in the matching submodule block.
#   4. Prefer specific names such as BIND_FAILED over generic FAILED.
#
# ============================================================================
# Module
# ============================================================================

module ENT 0

# ----------------------------------------------------------------------------
# SYS: common status shared by ENT core
# ----------------------------------------------------------------------------
submodule SYS 0
NORMAL              ok  0  success
ALREADY_INITIALIZED ok  1  ent already initialized
CLOSE_UNINITIALIZED err 1  ent not initialized for close
RUN_UNINITIALIZED   err 2  ent not initialized for run
INVALID_MSGCODE    err  3  invalid message code parts: module[%u] submodule[%u] inner[%u]

# ----------------------------------------------------------------------------
# INIT: ENT_Init / ENT_Close / runtime init flow
# ----------------------------------------------------------------------------
submodule INIT 1
INVALID_ARGUMENT  err  1  invalid init argument: name[%s] workPath[%s]
LOGPATH_ALLOCFAIL err  2  allocate log path failed, size[%d]
LOGNAME_ALLOCFAIL err  3  allocate log name failed, size[%d]
LOG_INITFAIL      err  4  ENT_LogInit failed, status[%d]
DEFAULT_HANDLEFAIL err 5  default log handle init failed, status[%d]
DEFAULT_LEVELFAIL err  6  default log level setup failed, status[%d]
ENTITY_HANDLEFAIL err  7  entity log handle init failed, status[%d]
ENTITY_LEVELFAIL  err  8  entity log level setup failed, status[%d]
LOCK_INITFAIL     err  9  entity lock init failed, status[%d]
CV_INITFAIL       err 10  entity condition variable init failed, status[%d]

# ----------------------------------------------------------------------------
# RT: realtime mode and post-init realtime attribute setup
# ----------------------------------------------------------------------------
submodule RT 2
NOT_INITIALIZED     err 1  ent not initialized for realtime attributes
BAD_POLICY          err 2  invalid realtime policy[%d]
BAD_CPU             err 3  invalid realtime cpu[%d]
AFFINITY_FAILED     err 4  set realtime cpu affinity failed, status[%d]
SCHED_QUERYFAIL     err 5  query realtime schedule priority range failed, errno[%d]
SCHED_SETFAIL       err 6  set realtime scheduling policy failed, status[%d]
UNSUPPORTED_PLATFORM err 7 realtime cpu or policy is unsupported on this platform
NOTRT              err 8  realtime attributes require realtime init mode

# ----------------------------------------------------------------------------
# THRD: ENT thread service
# ----------------------------------------------------------------------------
submodule THRD 3
INVALID_ARGUMENT err 1  invalid thread api argument
INVALID_HANDLE   err 2  invalid thread handle
ALLOC_FAILED     err 3  allocate thread context failed
CREATE_FAILED    err 4  create thread failed
WAIT_FAILED      err 5  wait thread failed
WAIT_TIMEOUT     ok  1  wait thread timeout
NOT_FOUND        err 6  thread id not found
ATTR_FAILED      err 7  configure thread attribute failed
LOCK_FAILED      err 8  thread lock operation failed

# ----------------------------------------------------------------------------
# UTHD: utility lock / cv service
# ----------------------------------------------------------------------------
submodule UTHD 4
INVALID_ARGUMENT err 1  invalid lock or condition-variable argument
ALLOC_FAILED     err 2  allocate lock or condition-variable context failed
INIT_FAILED      err 3  initialize lock or condition-variable object failed
INVALID_TYPE     err 4  invalid lock type
RWMODE_REQUIRED  err 5  rw lock requires explicit mode
WAIT_FAILED      err 6  condition wait failed
UNSUPPORTED_LOCK err 7  condition wait does not support this lock type
CLOCK_FAILED     err 8  clock operation failed

# ----------------------------------------------------------------------------
# SOCK: socket service
# ----------------------------------------------------------------------------
submodule SOCK 5
INIT_FAILED         err  1  socket runtime initialization failed
VERSION_UNSUPPORTED err  2  socket runtime version unsupported
NOT_INITIALIZED     err  3  socket runtime is not initialized
BAD_ARGUMENT        err  4  invalid socket argument
CREATE_FAILED       err  5  create socket failed
BIND_FAILED         err  6  bind socket failed
CONNECT_FAILED      err  7  connect socket failed
LISTEN_FAILED       err  8  listen socket failed
ACCEPT_FAILED       err  9  accept socket failed
CLOSE_FAILED        err 10  close socket failed
RECV_FAILED         err 11  receive socket data failed
SEND_FAILED         err 12  send socket data failed
SETOPT_FAILED       err 13  set socket option failed
SHUTDOWN_FAILED     err 14  shutdown socket failed
GETOPT_FAILED       err 15  get socket option failed
OPERATION_FAILED    err 16  socket operation failed

# ----------------------------------------------------------------------------
# TMR: timer service
# ----------------------------------------------------------------------------
submodule TMR 6
NOT_INITIALIZED    err  1  timer runtime is not initialized
BAD_ARGUMENT       err  2  invalid timer argument
ALLOC_FAILED       err  3  allocate timer context failed
CREATE_FAILED      err  4  create timer failed
START_FAILED       err  5  start timer failed
DELETE_FAILED      err  6  delete timer failed
LIST_FAILED        err  7  timer list operation failed
THREAD_FAILED      err  8  timer worker thread operation failed
RT_FAILED          err  9  timer realtime setup failed
UNSUPPORTED        err 10  timer feature is unsupported on this platform
ALREADY_INITIALIZED ok  1  timer runtime already initialized

# ----------------------------------------------------------------------------
# TPL: thread pool service
# ----------------------------------------------------------------------------
submodule TPL 7
BAD_ARGUMENT      err 1  invalid thread pool argument
ALLOC_FAILED      err 2  allocate thread pool resource failed
THREAD_INITFAIL   err 3  initialize thread service for pool failed
LOCK_INITFAIL     err 4  initialize thread pool lock failed
RECYCLE_LOCKFAIL  err 5  initialize recycle lock failed
CV_INITFAIL       err 6  initialize thread pool condition variable failed
WORKER_CREATEFAIL err 7  create thread pool worker failed
NOT_INITIALIZED   err 8  thread pool is not initialized
TASK_ALLOCFAIL    err 9  allocate thread pool task failed

# ----------------------------------------------------------------------------
# DLL: double-linked-list service
# ----------------------------------------------------------------------------
submodule DLL 8
BAD_ARGUMENT err 1  invalid double-linked-list argument
SAME_NODE    err 2  inserting list head into itself is invalid
EMPTY_LIST   err 3  list has no element
HEAD_NODE    err 4  current node is head and cannot be removed

# ----------------------------------------------------------------------------
# SCR: script engine service
# ----------------------------------------------------------------------------
submodule SCR 9
NOT_INITIALIZED err 1  script engine is not initialized
ALREADY_INIT    err 2  script engine has already been initialized
BAD_ARGUMENT    err 3  invalid script api argument
LOAD_FAILED     err 4  script root path is invalid or too long
COMPILE_FAILED  err 5  load script chunk failed, file[%s]
RUNTIME_FAILED  err 6  execute script chunk failed, file[%s]
FUNC_NOTFOUND   err 7  script function not found, fn[%s]
UNSUPPORTED     err 8  script runtime is not enabled in current build
