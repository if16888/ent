#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ient_db.h"
#include "ent_msg.h"

/* Compiled only with the PostgreSQL lexer test hook, no DB server needed. */
extern MSG_ID_T ENT_DbPgSQLRewriteForTest(const char*, size_t, char**);

static int expect_sql(const char* sql, size_t count, MSG_ID_T expected_status,
                      const char* expected)
{
    char* rewrite = NULL;
    MSG_ID_T status = ENT_DbPgSQLRewriteForTest(sql, count, &rewrite);
    int mismatch = status != expected_status;
    if(!mismatch && expected != NULL)
        mismatch = rewrite == NULL || strcmp(rewrite, expected) != 0;
    if(!mismatch && expected == NULL)
        mismatch = rewrite != NULL;
    if(mismatch)
        fprintf(stderr, "pgsql lex failed: status %d wanted %d, rewrite [%s], query [%s]\n",
                status, expected_status, rewrite == NULL ? "null" : rewrite, sql);
    free(rewrite);
    return mismatch;
}

int main(void)
{
    int errors = 0;
    errors += expect_sql("SELECT ?, '?', \"?\"", 1, ENT_SYS_NORMAL,
                         "SELECT $1, '?', \"?\"");
    errors += expect_sql("SELECT ? /* nested ? /* $2 */ ? */", 1, ENT_SYS_NORMAL,
                         "SELECT $1 /* nested ? /* $2 */ ? */");
    errors += expect_sql("SELECT -- ignored ? $9\n ?", 1, ENT_SYS_NORMAL,
                         "SELECT -- ignored ? $9\n $1");
    errors += expect_sql("SELECT $$? $1$$, ?, $tag$ ? $2 $tag$", 1, ENT_SYS_NORMAL,
                         "SELECT $$? $1$$, $1, $tag$ ? $2 $tag$");
    errors += expect_sql("SELECT E'it\\'s ? $3', ?", 1, ENT_SYS_NORMAL,
                         "SELECT E'it\\'s ? $3', $1");
    errors += expect_sql("SELECT $1, '$2', /* $3 */ $$?$$", 1, ENT_SYS_NORMAL,
                         "SELECT $1, '$2', /* $3 */ $$?$$");
    errors += expect_sql("SELECT $1234567890123456789012345", 1, ENT_DBS_BAD_PARAMS, NULL);
    errors += expect_sql("SELECT $0", 0, ENT_DBS_BAD_PARAMS, NULL);
    errors += expect_sql("SELECT $1, ?", 1, ENT_DBS_BAD_PARAMS, NULL);
    errors += expect_sql("SELECT ? /* unclosed", 1, ENT_DBS_BAD_PARAMS, NULL);
    errors += expect_sql("SELECT $$unclosed ?", 0, ENT_DBS_BAD_PARAMS, NULL);
    errors += expect_sql("SELECT ?, ?", 1, ENT_DBS_PARAM_COUNT, NULL);
    if(!errors) puts("PostgreSQL lexical placeholder rewrite: PASS");
    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
