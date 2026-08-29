#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_LINE_LEN 256

typedef enum
{
    TX_INACTIVE,
    TX_ACTIVE,
    TX_COMMITTED,
    TX_ABORTED
} TxState;

// טבלת דפים משותפת המוגנת על ידי Mutex
typedef struct
{
    char key[64];
    int value;
} Page;

typedef struct
{
    Page pages[1000];
    int page_count;
    pthread_mutex_t lock;
    // מנעול לסנכרון גישת חוטים
} SharedDatabase;

// רשומת לוג לצורך ביטול
typedef struct
{
    int tx_id;
    char key[64];
    int old_val;
    int new_val;
} LogEntry;

// ארגומנט שמועבר לכל Worker
Thread typedef struct
{
    int target_tx_id;
    LogEntry *log_history;
    int log_size;
    SharedDatabase *db;
} ThreadArgs;

/*
Shell like Program Plan:
Process Managing - kernel copies and connect them
---
work with kernel = system calls only
---
mini-bash$ prompt print
ready for input
read call
parse - split into tokens the line(' ' / tab as accepted spaces)
idintify and exe - internal / external and use fork execve

*/