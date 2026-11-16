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
    pthread_mutex_init(&self->mtx, NULL);
    self->buffer = malloc(sizeof(log_record_t)*config.buffer_size);
    pthread_mutex_init(&self->estagiario, NULL);

    /* TODO: Adicionar código aqui se necessário! */

    plog("[wal] Initialized\n");
}

void wal_finalize(wal_t *self)
{
    disk_finalize(self->disk);
    sem_destroy(&self->sem);
    pthread_mutex_destroy(&self->mtx);
    free(self->buffer);
    pthread_mutex_destroy(&self->estagiario);
    /* TODO: Adicionar código aqui se necessário! */

    plog("[wal] Finalized\n");
}

long wal_append(wal_t *self, const char *from, const char *to, int amount)
{
    sem_wait(&self->sem); // limita a escrita no buffer 
    // cria log para inserir no buffer
    log_record_t record;
    record.amount = amount;
    strcpy(record.from, from);
    strcpy(record.to, to);
    pthread_mutex_lock(&self->estagiario);
    self->disk->last_lsn++;
    pthread_mutex_unlock(&self->estagiario);
    record.lsn = disk_durable_lsn(self->disk);
    
    self->buffer[record.lsn%config.buffer_size] = record;

    /* TODO: Implementar! O código abaixo deve ser modificado! */
    
    return record.lsn;
}

void wal_wait_durable(wal_t *self, long lsn)
{
    /* TODO: Implementar! */
    if (disk_durable_lsn(self->disk) < lsn){
        pthread_mutex_lock(&self->mtx);
    }
}

void *wal_writer_run(void *arg)
{
    wal_t *self = (wal_t *)arg;

    plog("[wal] Log writer is now running!\n");

    /* TODO: Implementar! Enquanto o log não for encerrado, retire todos os
       registros disponíveis no buffer e grave-os com disk_write_batch(). */
    disk_write_batch(self->disk, self->buffer, config.buffer_size);
    for (int i = 0; i < config.buffer_size; i++){
        sem_post(&self->sem);
    }
    (void)self;

    pthread_mutex_unlock(&self->mtx);
    plog("[wal] Log writer has finished!\n");
    return NULL;
}
