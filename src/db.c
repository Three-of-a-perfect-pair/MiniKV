/*
 * db.c - O banco: consultas, transferências e snapshots.
 *
 * Implemente as funções de sincronização (db_acquire_*, db_release_*,
 * db_snapshot_begin/end). As operações db_get, db_transfer e db_snapshot, no
 * final do arquivo, já estão prontas e NÃO devem ser alteradas.
 *
 * VOCÊ DEVE ALTERAR ESTE ARQUIVO.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include "db.h"

void db_init(db_t *self, wal_t *wal)
{
    /* Cria as contas (NÃO ALTERE!). */
    char key[KEY_LEN];
    self->wal = wal;
    table_init(&self->table, config.accounts);
    for (unsigned int i = 0; i < config.accounts; i++) {
        account_key(i, key);
        table_insert(&self->table, key, INITIAL_BALANCE);
    }

    self->rw_locks = malloc(config.partitions * sizeof(pthread_rwlock_t));

    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setkind_np(&attr,
                                  PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    for (unsigned int i = 0; i < config.partitions; i++) {
        pthread_rwlock_init(&self->rw_locks[i], &attr);
    }
    pthread_rwlockattr_destroy(&attr);

    plog("[db] Initialized\n");
}

void db_finalize(db_t *self)
{
    for (unsigned int i = 0; i < config.partitions; i++) {
        pthread_rwlock_destroy(&self->rw_locks[i]);
    }

    free(self->rw_locks);

    table_finalize(&self->table);
    plog("[db] Finalized\n");
}

void db_acquire_read(db_t *self, const char *key)
{
    pthread_rwlock_rdlock(&self->rw_locks[db_partition_of(self, key)]);
}

void db_release_read(db_t *self, const char *key)
{
    pthread_rwlock_unlock(&self->rw_locks[db_partition_of(self, key)]);
}

void db_acquire_pair(db_t *self, const char *from, const char *to)
{
    unsigned int partition_from = db_partition_of(self, from);
    unsigned int partition_to = db_partition_of(self, to);
    if (partition_from == partition_to) {
        pthread_rwlock_wrlock(&self->rw_locks[partition_from]);
    } else if (partition_from < partition_to) {
        pthread_rwlock_wrlock(&self->rw_locks[partition_from]);
        pthread_rwlock_wrlock(&self->rw_locks[partition_to]);
    } else {
        pthread_rwlock_wrlock(&self->rw_locks[partition_to]);
        pthread_rwlock_wrlock(&self->rw_locks[partition_from]);
    }
}

void db_release_pair(db_t *self, const char *from, const char *to)
{
    unsigned int partition_from = db_partition_of(self, from);
    unsigned int partition_to = db_partition_of(self, to);
    pthread_rwlock_unlock(&self->rw_locks[partition_from]);
    if (partition_from != partition_to) {
        pthread_rwlock_unlock(&self->rw_locks[partition_to]);
    }
}

void db_snapshot_begin(db_t *self) {}

void db_snapshot_end(db_t *self) {}

/* --------------------------------------------------------- */
/* ATENÇÃO: Não será necessário modificar as funções abaixo! */
/* --------------------------------------------------------- */

unsigned int db_partition_of(db_t *self, const char *key)
{
    return table_hash(key) % config.partitions;
}

int db_get(db_t *self, const char *key)
{
    db_acquire_read(self, key);
    int balance = table_get(&self->table, key);
    db_release_read(self, key);
    return balance;
}

long db_transfer(db_t *self, const char *from, const char *to, int amount)
{
    long lsn = -1;

    /* Trava as duas contas e, se houver saldo, transfere e registra no log. */
    db_acquire_pair(self, from, to);
    if (table_get(&self->table, from) >= amount) {
        table_add(&self->table, from, -amount);
        table_add(&self->table, to, amount);
        lsn = wal_append(self->wal, from, to, amount);
    }
    db_release_pair(self, from, to);

    /* Só confirma ao cliente depois que o registro estiver em disco. */
    if (lsn >= 0)
        wal_wait_durable(self->wal, lsn);

    return lsn;
}

double db_snapshot(db_t *self, int *balances)
{
    double start = now_ms();
    db_snapshot_begin(self);
    double waited = now_ms() - start;

    for (unsigned int i = 0; i < table_size(&self->table); i++)
        balances[i] = table_value_at(&self->table, i);

    db_snapshot_end(self);
    return waited;
}
