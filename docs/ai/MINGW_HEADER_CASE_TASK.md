# Case-sensitive Windows SDK include task

Goal: permit existing Windows sources to build on a case-sensitive MinGW host.
Non-goals: change log/socket behavior, lifecycle, exports or SDK dependencies.
Allowed: spelling Windows.h/Winsock2.h as windows.h/winsock2.h in the five existing Windows branches.
Forbidden: any executable logic, public ABI, messages or CI behavior changes.
Risk: R1; implementation authorized by the foundation maintenance request.
Read-only scan: six uppercase includes; all other Windows SDK includes already
use lowercase. Native Windows accepts either spelling; Linux MinGW requires
windows.h. This is within public core scope and adds no capability.
Validation: git diff --check; cross configure/build shared/static utilities;
Linux full regression. Native Windows execution remains unverified locally.

Cross-build follow-up impact scan: SDK link names Ws2_32/Winmm also require
lowercase on case-sensitive hosts. MinGW additionally warns that __declspec(thread)
in utl_timer.c is ignored, making callback guard variables process-global.
Allowed scope expanded to the two library spellings and compiler-selected TLS
storage only (matching ent_init.c's existing MSVC/GNU pattern). No timer algorithm
or lifecycle changes. Risk R3 for the TLS correction; native Windows callback
concurrency execution remains required. Cross-build must emit no ignored-TLS
warnings; inspect MinGW emutls symbols and rerun the full Linux suite.
