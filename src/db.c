/*
 * db.c - O banco: consultas, transferências e snapshots.
 *
 * Implemente as funções de sincronização (db_acquire_*, db_release_*,
 * db_snapshot_begin/end). As operações db_get, db_transfer e db_snapshot, no
 * final do arquivo, já estão prontas e NÃO devem ser alteradas.
 *
 * VOCÊ DEVE ALTERAR ESTE ARQUIVO.
 */

#include <semaphore.h>
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

    self->partition_locks = malloc(config.partitions * sizeof(sem_t));
    self->writer_lock = malloc(config.partitions * sizeof(pthread_mutex_t));
    self->readers = calloc(config.partitions, sizeof(int));

    for (unsigned int i = 0; i < config.partitions; i++) {
        sem_init(&self->partition_locks[i], 0, 1);
        pthread_mutex_init(&self->writer_lock[i], NULL);
    }

    plog("[db] Initialized\n");
}

void db_finalize(db_t *self)
{
    for (unsigned int i = 0; i < config.partitions; i++) {
        sem_destroy(&self->partition_locks[i]);
        pthread_mutex_destroy(&self->writer_lock[i]);
    }

    free(self->partition_locks);
    free(self->writer_lock);
    free(self->readers);

    table_finalize(&self->table);
    plog("[db] Finalized\n");
}

void db_acquire_read(db_t *self, const char *key)
{
    unsigned int partition = db_partition_of(self, key);

    pthread_mutex_lock(&self->writer_lock[partition]);
    if (++self->readers[partition] == 1) {
        sem_wait(&self->partition_locks[partition]);
    }
    pthread_mutex_unlock(&self->writer_lock[partition]);
}

void db_release_read(db_t *self, const char *key)
{
    unsigned int partition = db_partition_of(self, key);

    pthread_mutex_lock(&self->writer_lock[partition]);
    if (--self->readers[partition] == 0) {
        sem_post(&self->partition_locks[partition]);
    }
    pthread_mutex_unlock(&self->writer_lock[partition]);
}

void db_acquire_pair(db_t *self, const char *from, const char *to)
{
    unsigned int partition_from = db_partition_of(self, from);
    unsigned int partition_to = db_partition_of(self, to);
    if (partition_from == partition_to) {
        sem_wait(&self->partition_locks[partition_from]);
    } else if (partition_from < partition_to) {
        sem_wait(&self->partition_locks[partition_from]);
        sem_wait(&self->partition_locks[partition_to]);
    } else {
        sem_wait(&self->partition_locks[partition_to]);
        sem_wait(&self->partition_locks[partition_from]);
    }
}

void db_release_pair(db_t *self, const char *from, const char *to)
{
    unsigned int partition_from = db_partition_of(self, from);
    unsigned int partition_to = db_partition_of(self, to);
    sem_post(&self->partition_locks[partition_from]);
    if (partition_from != partition_to) {
        sem_post(&self->partition_locks[partition_to]);
    }
}

void db_snapshot_begin(db_t *self)
{
    // Precisa travar todas as transferências
    for (int i = 0; i < config.partitions; i++)
        sem_wait(&self->partition_locks[i]);
}

void db_snapshot_end(db_t *self)
{
    /* TODO: Implementar! */
    for (int i = 0; i < config.partitions; i++)
        sem_post(&self->partition_locks[i]);
}

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
