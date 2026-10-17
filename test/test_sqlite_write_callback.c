#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "ient_db.h"
#include "ent_msg.h"

typedef struct {
    int calls;
    long long affected;
    int columns;
} CAPTURE;

static void capture_write(char** names, char** values, long long rows,
                          int columns, void* opaque)
{
    CAPTURE* got = (CAPTURE*)opaque;
    (void)names;
    (void)values;
    got->calls++;
    got->affected = rows;
    got->columns = columns;
}

static int write_and_expect(sqlite3* db, const char* sql, CAPTURE* c,
                            long long expected)
{
    int before = c->calls;
    if(ENT_DbSqliteWrite(db, sql, capture_write, c) != ENT_SYS_NORMAL ||
       c->calls != before + 1 || c->affected != expected || c->columns != 0)
    {
        fprintf(stderr, "SQLite DML callback contract failed: %s\n", sql);
        return 1;
    }
    return 0;
}

int main(void)
{
    sqlite3* db = NULL;
    CAPTURE c = {0, 0, 0};
    int fail = 0;
    if(sqlite3_open(":memory:", &db) != SQLITE_OK) return EXIT_FAILURE;
    if(ENT_DbSqliteWrite(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)", NULL, NULL) != ENT_SYS_NORMAL)
        fail = 1;
    if(!fail && write_and_expect(db, "INSERT INTO t(v) VALUES ('a')", &c, 1)) fail = 1;
    if(!fail && write_and_expect(db, "CREATE INDEX idx_t_v ON t(v)", &c, 0)) fail = 1;
    if(!fail && write_and_expect(db, "UPDATE t SET v='b' WHERE id=1", &c, 1)) fail = 1;
    if(!fail && write_and_expect(db, "UPDATE t SET v='b' WHERE id=404", &c, 0)) fail = 1;
    if(!fail && write_and_expect(db, "DELETE FROM t WHERE id=1", &c, 1)) fail = 1;
    if(!fail)
    {
        int before = c.calls;
        if(ENT_DbSqliteWrite(db, "not valid SQL", capture_write, &c) == ENT_SYS_NORMAL ||
           c.calls != before)
            fail = 1;
    }
    if(!fail && ENT_DbSqliteRead(db, "SELECT 1 AS value", NULL, NULL) != ENT_SYS_NORMAL)
        fail = 1;
    if(sqlite3_close(db) != SQLITE_OK) fail = 1;
    if(!fail) puts("SQLite legacy callback counts and empty reads: PASS");
    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
