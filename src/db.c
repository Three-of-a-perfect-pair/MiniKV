/*
 * db.c - O banco: consultas, transferências e snapshots.
 *
 * Implemente as funções de sincronização (db_acquire_*, db_release_*,
 * db_snapshot_begin/end). As operações db_get, db_transfer e db_snapshot, no
 * final do arquivo, já estão prontas e NÃO devem ser alteradas.
 *
 * VOCÊ DEVE ALTERAR ESTE ARQUIVO.
 */

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

    self->mutex = malloc(sizeof(pthread_mutex_t) * config.partitions);

    self->readers_cv = malloc(sizeof(pthread_cond_t) * config.partitions);
    self->writers_cv = malloc(sizeof(pthread_cond_t) * config.partitions);
    self->snapshots_cv = malloc(sizeof(pthread_cond_t) * config.partitions);

    self->readers = malloc(sizeof(int) * config.partitions);
    self->writers = malloc(sizeof(int) * config.partitions);
    self->snapshots = malloc(sizeof(int) * config.partitions);

    self->curr_ticket = malloc(sizeof(int) * config.partitions);
    self->next_ticket = malloc(sizeof(int) * config.partitions);

    self->waiting_writers = malloc(sizeof(int) * config.partitions);
    self->waiting_snapshots = malloc(sizeof(int) * config.partitions);

    for (unsigned int i = 0; i < config.partitions; i++) {
        pthread_mutex_init(&self->mutex[i], NULL);

        pthread_cond_init(&self->readers_cv[i], NULL);
        pthread_cond_init(&self->writers_cv[i], NULL);
        pthread_cond_init(&self->snapshots_cv[i], NULL);

        self->readers[i] = 0;
        self->writers[i] = 0;
        self->snapshots[i] = 0;

        self->curr_ticket[i] = 0;
        self->next_ticket[i] = 0;

        self->waiting_writers[i] = 0;
        self->waiting_snapshots[i] = 0;
    }

    plog("[db] Initialized\n");
}

void db_finalize(db_t *self)
{
    for (unsigned int i = 0; i < config.partitions; i++) {
        pthread_mutex_destroy(&self->mutex[i]);

        pthread_cond_destroy(&self->readers_cv[i]);
        pthread_cond_destroy(&self->writers_cv[i]);
        pthread_cond_destroy(&self->snapshots_cv[i]);
    }

    free(self->mutex);

    free(self->readers_cv);
    free(self->writers_cv);
    free(self->snapshots_cv);

    free(self->readers);
    free(self->writers);
    free(self->snapshots);

    free(self->curr_ticket);
    free(self->next_ticket);

    free(self->waiting_writers);
    free(self->waiting_snapshots);

    table_finalize(&self->table);
    plog("[db] Finalized\n");
}

void db_acquire_read(db_t *self, const char *key)
{
    unsigned int part = db_partition_of(self, key);
    pthread_mutex_lock(&self->mutex[part]);

    // priority to writers and waiting writers except during snapshots
    while (self->writers[part] ||
           (self->waiting_writers[part] && !self->snapshots[part])) {
        pthread_cond_wait(&self->readers_cv[part], &self->mutex[part]);
    }
    self->readers[part]++;
    pthread_mutex_unlock(&self->mutex[part]);
}

void db_release_read(db_t *self, const char *key)
{
    unsigned int part = db_partition_of(self, key);

    pthread_mutex_lock(&self->mutex[part]);
    self->readers[part]--;
    pthread_cond_broadcast(&self->writers_cv[part]);
    pthread_mutex_unlock(&self->mutex[part]);
}

void db_acquire_pair(db_t *self, const char *from, const char *to)
{
    unsigned int part_from = db_partition_of(self, from);
    unsigned int part_to = db_partition_of(self, to);
    unsigned int minor_part = part_from < part_to ? part_from : part_to;
    unsigned int major_part = part_from < part_to ? part_to : part_from;

    pthread_mutex_lock(&self->mutex[minor_part]);

    int ticket = self->next_ticket[minor_part]++;

    self->waiting_writers[minor_part]++;

    // only goes out when no writers, no readers, no snapshots and no waiting
    // snapshots
    while (ticket != self->curr_ticket[minor_part] ||
           self->writers[minor_part] || self->readers[minor_part] ||
           self->snapshots[minor_part] || self->waiting_snapshots[minor_part])
        pthread_cond_wait(&self->writers_cv[minor_part],
                          &self->mutex[minor_part]);

    self->waiting_writers[minor_part]--;
    self->writers[minor_part]++;

    pthread_mutex_unlock(&self->mutex[minor_part]);

    // same but with major partition
    if (minor_part != major_part) {
        pthread_mutex_lock(&self->mutex[major_part]);

        ticket = self->next_ticket[major_part]++;
        self->waiting_writers[major_part]++;

        while (ticket != self->curr_ticket[major_part] ||
               self->writers[major_part] || self->readers[major_part] ||
               self->snapshots[major_part] ||
               self->waiting_snapshots[major_part])
            pthread_cond_wait(&self->writers_cv[major_part],
                              &self->mutex[major_part]);

        self->waiting_writers[major_part]--;
        self->writers[major_part]++;

        pthread_mutex_unlock(&self->mutex[major_part]);
    }
}

void db_release_pair(db_t *self, const char *from, const char *to)
{
    unsigned int part_from = db_partition_of(self, from);
    unsigned int part_to = db_partition_of(self, to);
    unsigned int minor_part = part_from < part_to ? part_from : part_to;
    unsigned int major_part = part_from < part_to ? part_to : part_from;

    pthread_mutex_lock(&self->mutex[minor_part]);

    self->curr_ticket[minor_part]++;
    self->writers[minor_part]--;

    pthread_cond_broadcast(&self->readers_cv[minor_part]);
    pthread_cond_broadcast(&self->writers_cv[minor_part]);
    pthread_cond_broadcast(&self->snapshots_cv[minor_part]);

    pthread_mutex_unlock(&self->mutex[minor_part]);

    // same but with major partition
    if (minor_part != major_part) {
        pthread_mutex_lock(&self->mutex[major_part]);

        self->writers[major_part]--;
        self->curr_ticket[major_part]++;

        pthread_cond_broadcast(&self->readers_cv[major_part]);
        pthread_cond_broadcast(&self->writers_cv[major_part]);
        pthread_cond_broadcast(&self->snapshots_cv[major_part]);

        pthread_mutex_unlock(&self->mutex[major_part]);
    }
}

void db_snapshot_begin(db_t *self)
{
    for (unsigned int i = 0; i < config.partitions; i++) {
        pthread_mutex_lock(&self->mutex[i]);

        self->waiting_snapshots[i]++;

        // wait for writers to end
        while (self->writers[i]) {
            pthread_cond_wait(&self->snapshots_cv[i], &self->mutex[i]);
        }

        self->waiting_snapshots[i]--;
        self->snapshots[i]++;

        pthread_cond_broadcast(&self->readers_cv[i]);

        pthread_mutex_unlock(&self->mutex[i]);
    }
}

void db_snapshot_end(db_t *self)
{
    for (unsigned int i = 0; i < config.partitions; i++) {
        pthread_mutex_lock(&self->mutex[i]);

        self->snapshots[i]--;

        pthread_cond_broadcast(&self->readers_cv[i]);
        pthread_cond_broadcast(&self->writers_cv[i]);
        pthread_cond_broadcast(&self->snapshots_cv[i]);

        pthread_mutex_unlock(&self->mutex[i]);
    }
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
