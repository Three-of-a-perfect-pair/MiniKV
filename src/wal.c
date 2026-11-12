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
    sem_init(&self->sem, 0, config.buffer_size);  

    /* TODO: Adicionar código aqui se necessário! */

    plog("[wal] Initialized\n");
}

void wal_finalize(wal_t *self)
{
    disk_finalize(self->disk);
    /* TODO: Adicionar código aqui se necessário! */

    plog("[wal] Finalized\n");
}

long wal_append(wal_t *self, const char *from, const char *to, int amount)
{
    sem_wait(&self->sem);
    log_record_t *record = malloc(sizeof(log_record_t));
    record->amount = amount;
    strcpy(record->from, from);
    strcpy(record->to, to);
    self->disk->durable_lsn++;
    record->lsn = disk_durable_lsn(self->disk);
    //onde que grava esse record???
    

    /* TODO: Implementar! O código abaixo deve ser modificado! */
    
    return record->lsn;
}

void wal_wait_durable(wal_t *self, long lsn)
{
    /* TODO: Implementar! */
}

void *wal_writer_run(void *arg)
{
    wal_t *self = (wal_t *)arg;

    plog("[wal] Log writer is now running!\n");

    /* TODO: Implementar! Enquanto o log não for encerrado, retire todos os
       registros disponíveis no buffer e grave-os com disk_write_batch(). */
    (void)self;

    plog("[wal] Log writer has finished!\n");
    return NULL;
}
