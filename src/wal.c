/*
 * wal.c - O log (write-ahead log) com group commit.
 *
 * Implemente todas as funções deste arquivo.
 *
 * VOCÊ DEVE ALTERAR ESTE ARQUIVO.
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include "wal.h"
#include "config.h"
#include "storage.h"

void wal_init(wal_t *self, disk_t *disk)
{
    self->disk = disk;
    pthread_cond_init(&self->not_empty, NULL);
    pthread_cond_init(&self->space_available, NULL);
    pthread_cond_init(&self->durable_changed, NULL);

    pthread_mutex_init(&self->mtx, NULL);
    self->buffer = malloc(config.buffer_size * sizeof(log_record_t));
    self->head = 0;
    self->tail = 0;
    self->count = 0;
    self->next_lsn = 0;
    atomic_init(&self->finalize, 0);

    /* TODO: Adicionar código aqui se necessário! */

    plog("[wal] Initialized\n");
}

void wal_finalize(wal_t *self)
{
    pthread_mutex_destroy(&self->mtx);
    pthread_cond_destroy(&self->not_empty);
    pthread_cond_destroy(&self->space_available);
    pthread_cond_destroy(&self->durable_changed);
    free(self->buffer);
    /* TODO: Adicionar código aqui se necessário! */

    plog("[wal] Finalized\n");
}

long wal_append(wal_t *self, const char *from, const char *to, int amount)
{
    // cria log para inserir no buffer
    pthread_mutex_lock(&self->mtx);

    while (self->count == config.buffer_size)
        pthread_cond_wait(&self->space_available, &self->mtx);

    log_record_t record;
    record.amount = amount;
    strcpy(record.from, from);
    strcpy(record.to, to);
    record.lsn = self->next_lsn++;

    self->buffer[self->tail] = record;
    self->tail = (self->tail + 1) % config.buffer_size;
    self->count++;
    pthread_cond_signal(&self->not_empty);
    pthread_mutex_unlock(&self->mtx);

    /* TODO: Implementar! O código abaixo deve ser modificado! */

    return record.lsn;
}

void wal_wait_durable(wal_t *self, long lsn)
{
    /* TODO: Implementar! */
    pthread_mutex_lock(&self->mtx);

    while (disk_durable_lsn(self->disk) < lsn)
        pthread_cond_wait(&self->durable_changed, &self->mtx);

    pthread_mutex_unlock(&self->mtx);
}

void *wal_writer_run(void *arg)
{
    wal_t *self = (wal_t *)arg;

    plog("[wal] Log writer is now running!\n");

    while (!atomic_load(&self->finalize)) {
        pthread_mutex_lock(&self->mtx);

        while (self->count == 0 && !atomic_load(&self->finalize))
            pthread_cond_wait(&self->not_empty, &self->mtx);

        if (atomic_load(&self->finalize) && self->count == 0) {
            pthread_mutex_unlock(&self->mtx);
            break;
        }

        unsigned int batch_size = self->count;
        log_record_t local_batch[batch_size]; // escreve nesse local
                                              // enquanto tiver disponível.
                                              // Evita data race com o buffer
        for (int i = 0; i < batch_size; i++) {
            local_batch[i] = self->buffer[self->head];
            self->head = (self->head + 1) % config.buffer_size;
        }
        self->count = 0;

        pthread_cond_broadcast(&self->space_available);
        pthread_mutex_unlock(&self->mtx);

        /* TODO: Implementar! Enquanto o log não for encerrado, retire todos os
           registros disponíveis no buffer e grave-os com disk_write_batch(). */
        disk_write_batch(self->disk, local_batch, batch_size);

        pthread_mutex_lock(&self->mtx);
        pthread_cond_broadcast(&self->durable_changed);
        pthread_mutex_unlock(&self->mtx);
    }
    (void)self;

    plog("[wal] Log writer has finished!\n");
    return NULL;
}
