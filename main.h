#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>



#define STATE_BOX 0
#define STATE_CORRIDA 1
#define STATE_SEGURANCA 2
#define STATE_DESISTENCIA 3
#define STATE_TERMINADO 4
#define MUTEX 0
#define STATE_LIVRE 0
#define STATE_OCUPADA 1
#define STATE_RESERVADA 2
#define N 20
#define BUFFER_SIZE 1024
#define PIPE_NAME "named_pipe"

typedef struct namedpipe{
    char pipe_message[BUFFER_SIZE];
}namedpipe;

typedef struct _msgbuf {
    long carNumber;      // MANDATORY field: type of message  must be > 0
    char mtext[256];
    int tempo_parado;
}msgbuf;

typedef struct carro{
    int equipa;
    int estado_carro;
    double consumo;
    double velocidade;
    double combustivel;
    int gota;
    int numero_carro;
    int totalboxes;
    int carIDD;
    int percentagem_fiabilidade;
    int voltasFinal;
    pthread_t threadCarro;
    int exist;
    int avariado;
    int tAvaria;
    } carro;

typedef struct equipa{
  char nome[256];
  int estadoBox;
  int carroEspera;
  int nCarros;
  carro *listaCarros;
  pid_t pidEquipa;
  pthread_t threadBox;
} equipa;

typedef struct shared_mem{
  int configs[9];
  int nCar;
  sem_t sem_semm;
  sem_t sem_mutex;
  sem_t sem_raceStart;
  sem_t sem_car;
  int raceOn;
  int totalAvarias;
  int totalAbastecimentos;
  int racefinish;
  pthread_mutex_t pmutex;
  pthread_mutex_t bmutex;
  pthread_cond_t condvar;
  pthread_cond_t fullbox;
  pthread_cond_t emptybox;
}shared_mem;
