#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32
#include <pthread.h>
#endif

#include "ient_comm.h"
#include "ent_script.h"

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

#ifndef WIN32
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
    int start;
    int failed;
#if ENT_ENABLE_LUA
    const char* scriptName;
#endif
    const char* functionName;
    const ENT_SCRIPT_ARG_T* in;
    int iterations;
} TEST_SCRIPT_RACE_CTX;

static int iENT_ScriptReloadStatusAllowed(MSG_ID_T sts)
{
#if ENT_ENABLE_LUA
    return (sts == ENT_SYS_NORMAL ||
            sts == ENT_SCR_NOT_INITIALIZED ||
            sts == ENT_SCR_COMPILE_FAILED);
#else
    return (sts == ENT_SCR_NOT_INITIALIZED ||
            sts == ENT_SCR_UNSUPPORTED);
#endif
}

static int iENT_ScriptCallStatusAllowed(MSG_ID_T sts)
{
#if ENT_ENABLE_LUA
    return (sts == ENT_SYS_NORMAL ||
            sts == ENT_SCR_NOT_INITIALIZED ||
            sts == ENT_SCR_FUNC_NOTFOUND);
#else
    return (sts == ENT_SCR_NOT_INITIALIZED ||
            sts == ENT_SCR_UNSUPPORTED);
#endif
}

static void* iENT_ScriptRaceWorker(void* data)
{
    TEST_SCRIPT_RACE_CTX* ctx = (TEST_SCRIPT_RACE_CTX*)data;
    ENT_SCRIPT_RET_T out;
    int i = 0;

    pthread_mutex_lock(&ctx->mutex);
    ctx->ready = 1;
    pthread_cond_broadcast(&ctx->cond);
    while(ctx->start == 0)
    {
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }
    pthread_mutex_unlock(&ctx->mutex);

    for(i = 0; i < ctx->iterations; ++i)
    {
#if ENT_ENABLE_LUA
        MSG_ID_T sts = ENT_ScriptReload(ctx->scriptName);
        if(!iENT_ScriptReloadStatusAllowed(sts))
        {
            pthread_mutex_lock(&ctx->mutex);
            ctx->failed = 1;
            pthread_mutex_unlock(&ctx->mutex);
            return NULL;
        }
#endif

        {
            MSG_ID_T sts = ENT_ScriptCall(ctx->functionName, ctx->in, &out);
            if(!iENT_ScriptCallStatusAllowed(sts))
            {
                pthread_mutex_lock(&ctx->mutex);
                ctx->failed = 1;
                pthread_mutex_unlock(&ctx->mutex);
                return NULL;
            }
        }
    }

    return NULL;
}
#endif

int main(void)
{
    ENT_SCRIPT_RET_T out;
    MSG_ID_T sts = ENT_SYS_NORMAL;
    const char* scriptRoot = "/tmp";
#if ENT_ENABLE_LUA
    const char* scriptFile = "ent_script_case.lua";
    const char* scriptPath = "/tmp/ent_script_case.lua";
    FILE* fp = NULL;
    ENT_SCRIPT_KV_T kvs[4];
    ENT_SCRIPT_ARG_T in;
#endif

    sts = ENT_ScriptClose();
    if(expect_true(sts == ENT_SCR_NOT_INITIALIZED,
                   "ENT_ScriptClose should reject close when script engine is uninitialized") != 0)
    {
        return EXIT_FAILURE;
    }

    sts = ENT_ScriptInit(NULL);
    if(expect_true(sts == ENT_SCR_BAD_ARGUMENT,
                   "ENT_ScriptInit should reject NULL script root") != 0)
    {
        return EXIT_FAILURE;
    }

    sts = ENT_ScriptCall("calc_discount", NULL, &out);
    if(expect_true(sts == ENT_SCR_NOT_INITIALIZED,
                   "ENT_ScriptCall should reject calls before init") != 0)
    {
        return EXIT_FAILURE;
    }

    sts = ENT_ScriptInit(scriptRoot);
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_ScriptInit should initialize with a valid root path") != 0)
    {
        return EXIT_FAILURE;
    }

    sts = ENT_ScriptInit(scriptRoot);
    if(expect_true(sts == ENT_SCR_ALREADY_INIT,
                   "ENT_ScriptInit should report already initialized on repeated init") != 0)
    {
        return EXIT_FAILURE;
    }

    sts = ENT_ScriptReload(NULL);
    if(expect_true(sts == ENT_SCR_BAD_ARGUMENT,
                   "ENT_ScriptReload should reject NULL script name") != 0)
    {
        return EXIT_FAILURE;
    }

#if ENT_ENABLE_LUA
    fp = ENT_FOpen(scriptPath, "wb");
    if(expect_true(fp != NULL, "test should create a temporary lua script file") != 0)
    {
        return EXIT_FAILURE;
    }

    fprintf(fp, "function calc_discount(args)\n");
    fprintf(fp, "  local amount = tonumber(args[\"amount\"] or 0)\n");
    fprintf(fp, "  local vip = (args[\"vip\"] == true)\n");
    fprintf(fp, "  local tier = tostring(args[\"tier\"] or \"none\")\n");
    fprintf(fp, "  local note = args[\"note\"]\n");
    fprintf(fp, "  local ratio = 0.00\n");
    fprintf(fp, "  if amount >= 100 then\n");
    fprintf(fp, "    ratio = 0.10\n");
    fprintf(fp, "  end\n");
    fprintf(fp, "  if vip and amount >= 100 then\n");
    fprintf(fp, "    ratio = 0.15\n");
    fprintf(fp, "  end\n");
    fprintf(fp, "  if note ~= nil then\n");
    fprintf(fp, "    return {code = -1, message = \"note should be nil\"}\n");
    fprintf(fp, "  end\n");
    fprintf(fp, "  return {code = 0, message = string.format(\"discount=%%.2f;tier=%%s\", ratio, tier)}\n");
    fprintf(fp, "end\n");
    fclose(fp);

    sts = ENT_ScriptReload(scriptFile);
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_ScriptReload should load a valid lua script") != 0)
    {
        remove(scriptPath);
        return EXIT_FAILURE;
    }

    memset(kvs, 0, sizeof(kvs));
    kvs[0].key = "amount";
    kvs[0].type = ENT_SCRIPT_ARG_DOUBLE_E;
    kvs[0].numValue = 180.0;
    kvs[1].key = "vip";
    kvs[1].type = ENT_SCRIPT_ARG_BOOL_E;
    kvs[1].boolValue = true;
    kvs[2].key = "tier";
    kvs[2].type = ENT_SCRIPT_ARG_STRING_E;
    kvs[2].value = "gold";
    kvs[3].key = "note";
    kvs[3].type = ENT_SCRIPT_ARG_NULL_E;
    in.items = kvs;
    in.count = 4u;

    sts = ENT_ScriptCall("calc_discount", &in, &out);
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_ScriptCall should execute a loaded lua function") != 0)
    {
        remove(scriptPath);
        return EXIT_FAILURE;
    }

    if(expect_true(out.code == 0, "script return code should be parsed from lua table") != 0)
    {
        remove(scriptPath);
        return EXIT_FAILURE;
    }

    if(expect_true(strcmp(out.message, "discount=0.15;tier=gold") == 0,
                   "script return message should be parsed from lua table") != 0)
    {
        remove(scriptPath);
        return EXIT_FAILURE;
    }

    if(expect_true(ENT_ScriptCall("missing_fn", &in, &out) == ENT_SCR_FUNC_NOTFOUND,
                   "ENT_ScriptCall should report missing lua function") != 0)
    {
        remove(scriptPath);
        return EXIT_FAILURE;
    }

#ifndef WIN32
    {
        pthread_t raceThread;
        TEST_SCRIPT_RACE_CTX raceCtx;
        int i = 0;

        memset(&raceCtx, 0, sizeof(raceCtx));
        if(expect_true(pthread_mutex_init(&raceCtx.mutex, NULL) == 0,
                       "pthread_mutex_init should prepare the script race mutex") != 0)
        {
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        if(expect_true(pthread_cond_init(&raceCtx.cond, NULL) == 0,
                       "pthread_cond_init should prepare the script race condition") != 0)
        {
            pthread_mutex_destroy(&raceCtx.mutex);
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        raceCtx.functionName = "calc_discount";
        raceCtx.in = &in;
        raceCtx.iterations = 512;
#if ENT_ENABLE_LUA
        raceCtx.scriptName = scriptFile;
#endif

        if(expect_true(pthread_create(&raceThread, NULL, iENT_ScriptRaceWorker, &raceCtx) == 0,
                       "pthread_create should start the script race worker") != 0)
        {
            pthread_cond_destroy(&raceCtx.cond);
            pthread_mutex_destroy(&raceCtx.mutex);
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        if(expect_true(pthread_mutex_lock(&raceCtx.mutex) == 0,
                       "pthread_mutex_lock should synchronize with the script race worker") != 0)
        {
            pthread_join(raceThread, NULL);
            pthread_cond_destroy(&raceCtx.cond);
            pthread_mutex_destroy(&raceCtx.mutex);
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        while(raceCtx.ready == 0)
        {
            pthread_cond_wait(&raceCtx.cond, &raceCtx.mutex);
        }
        raceCtx.start = 1;
        pthread_cond_broadcast(&raceCtx.cond);
        pthread_mutex_unlock(&raceCtx.mutex);

        for(i = 0; i < 128; ++i)
        {
            sts = ENT_ScriptClose();
            if(expect_true(sts == ENT_SYS_NORMAL,
                           "ENT_ScriptClose should remain safe during concurrent access") != 0)
            {
                pthread_join(raceThread, NULL);
                pthread_cond_destroy(&raceCtx.cond);
                pthread_mutex_destroy(&raceCtx.mutex);
                remove(scriptPath);
                return EXIT_FAILURE;
            }

            sts = ENT_ScriptInit(scriptRoot);
            if(expect_true(sts == ENT_SYS_NORMAL || sts == ENT_SCR_ALREADY_INIT,
                           "ENT_ScriptInit should remain safe during concurrent access") != 0)
            {
                pthread_join(raceThread, NULL);
                pthread_cond_destroy(&raceCtx.cond);
                pthread_mutex_destroy(&raceCtx.mutex);
                remove(scriptPath);
                return EXIT_FAILURE;
            }
        }

        sts = ENT_ScriptInit(scriptRoot);
        if(expect_true(sts == ENT_SYS_NORMAL || sts == ENT_SCR_ALREADY_INIT,
                       "ENT_ScriptInit should leave the script engine initialized for cleanup") != 0)
        {
            pthread_join(raceThread, NULL);
            pthread_cond_destroy(&raceCtx.cond);
            pthread_mutex_destroy(&raceCtx.mutex);
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        if(expect_true(pthread_join(raceThread, NULL) == 0,
                       "pthread_join should join the script race worker") != 0)
        {
            pthread_cond_destroy(&raceCtx.cond);
            pthread_mutex_destroy(&raceCtx.mutex);
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        if(expect_true(raceCtx.failed == 0,
                       "concurrent script calls should only observe supported statuses") != 0)
        {
            pthread_cond_destroy(&raceCtx.cond);
            pthread_mutex_destroy(&raceCtx.mutex);
            remove(scriptPath);
            return EXIT_FAILURE;
        }

        pthread_cond_destroy(&raceCtx.cond);
        pthread_mutex_destroy(&raceCtx.mutex);
    }
#endif

    remove(scriptPath);
#else
    sts = ENT_ScriptCall("calc_discount", NULL, &out);
    if(expect_true(sts == ENT_SCR_UNSUPPORTED,
                   "ENT_ScriptCall should report unsupported when Lua runtime is not enabled") != 0)
    {
        return EXIT_FAILURE;
    }
#endif

    sts = ENT_ScriptClose();
    if(expect_true(sts == ENT_SYS_NORMAL,
                   "ENT_ScriptClose should succeed after init") != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
