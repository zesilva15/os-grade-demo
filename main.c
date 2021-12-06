#include "main.h"

key_t key; // key for msgget
int msqid; // id to the msgqueue
int shmid, semid; // ids da mem partilhada e dos semaforos
pid_t gestor_de_corrida_id;
pid_t gestor_de_avarias_id;
shared_mem * mem;
sem_t * semm, * mutex, * raceStart, * sem_car;
equipa * equipas;
carro * carros;

void estatisticas(){
  printf("\n");
  for (size_t i = mem->configs[2]; i >0; i--) {
    for (size_t j = 0; j < mem->configs[3]; j++) {
      for (size_t k = 0; k < mem->configs[4]; k++) {
        if (equipas[j].listaCarros[k].voltasFinal==i) {
          printf("O carro %d da equipa %d acabou com %d voltas e %d paragens na box.\n",equipas[j].listaCarros[k].numero_carro,j,equipas[j].listaCarros[k].voltasFinal,equipas[j].listaCarros[k].totalboxes);
        }
      }
    }
  }
  printf("A corrida terminou com %d avarias\n", mem->totalAvarias);
  printf("A corrida terminou com %d abastecimentos\n", mem->totalAbastecimentos);
}

void escreverLog(char * message) { //Função que recebe a mensagem dada pelo processo/thread e escreve-a no ficheiro de log com o tempo do instante.
   FILE * fp;
   fp = fopen("log.txt", "a");
   time_t rawtime;
   struct tm * timeinfo;
   time( & rawtime);
   timeinfo = localtime( & rawtime);
   char horas[256];
   sprintf(horas, "%02d:%02d:%02d ", timeinfo -> tm_hour, timeinfo -> tm_min, timeinfo -> tm_sec);
   strcat(horas, message);
   fprintf(fp, "%s\n", horas);
   fclose(fp);
}

void cleanup(int signo) // invocada pelo monitor limpa recursos e termina o programa
{
   for (int i = mem -> configs[3] - 1; i >= 0; i--) {
      kill(equipas[i].pidEquipa, SIGKILL);
   }
   kill(gestor_de_corrida_id, SIGKILL);
   kill(gestor_de_avarias_id, SIGKILL);
   if (shmid >= 0)
      shmctl(shmid, IPC_RMID, NULL);
   msgctl(msqid, IPC_RMID, 0);
   estatisticas();
   printf("Closing...\n");
   escreverLog("SIMULATOR CLOSING");
   exit(0);
}



int * readConfig() { // lê o ficheiro config.txt e devolve uma array com os valores da mesma
   char nomeFich[] = "config.txt";
   static int config[7];
   FILE * f = fopen(nomeFich, "r");
   char x[1024];
   int i = 0;
   while (fscanf(f, " %1023s", x) == 1) {
      config[i] = atoi(x);
      i++;
   }
   fclose(f);
   return config;
}

void * threadCarro(void * threadid) // thread carro, de momento apenas criada
{
    msgbuf avariaRec;
    char log[256];
   int carID = * ((int * ) threadid);
   printf("[%d] Carro criado com id [%d]\n", getpid(), carID);
   sem_post(sem_car);
   int distPercorridaVolta=0, nVoltas=0;
   double combustivelPorVolta;
   int nCarro,indexCarro,indexEquipa;
   while (1) {
     pthread_mutex_lock( & mem -> pmutex);
     while (mem->raceOn != 1) {
       pthread_cond_wait(&mem->condvar,&mem->pmutex);
       for (size_t i = 0; i < mem -> configs[3]; i++) {
          for (size_t j = 0; j < mem -> configs[4]; j++) {
              if(equipas[i].listaCarros[j].carIDD==carID){
                indexEquipa=i;
                indexCarro=j;
                nCarro = equipas[indexEquipa].listaCarros[indexCarro].numero_carro;
                equipas[indexEquipa].listaCarros[indexCarro].estado_carro=STATE_CORRIDA;
                equipas[indexEquipa].listaCarros[indexCarro].combustivel=mem->configs[6];
                equipas[indexEquipa].listaCarros[indexCarro].gota=0;
                equipas[indexEquipa].listaCarros[indexCarro].totalboxes=0;
                combustivelPorVolta=(mem->configs[1]/equipas[indexEquipa].listaCarros[indexCarro].velocidade)*equipas[indexEquipa].listaCarros[indexCarro].consumo;
              }
          }
       }
     }

     if(msgrcv(msqid, & avariaRec, sizeof(avariaRec), nCarro, IPC_NOWAIT)>0){ // rececao do message queue
       printf("Carro nr %d recebeu uma avaria e vai estar %ds parado!\n", nCarro,avariaRec.tempo_parado);
       sprintf(log, "CAR %d HAS A MALFUNCTION WITH %d SECONDS STOP", nCarro,avariaRec.tempo_parado);
       escreverLog(log);
       if(equipas[indexEquipa].listaCarros[indexCarro].estado_carro==STATE_CORRIDA){
         equipas[indexEquipa].listaCarros[indexCarro].estado_carro=STATE_SEGURANCA;
         sprintf(log, "CAR %d IN SAFETY MODE", nCarro);
         escreverLog(log);
         equipas[indexEquipa].listaCarros[indexCarro].velocidade=0.3*equipas[indexEquipa].listaCarros[indexCarro].velocidade;
         equipas[indexEquipa].listaCarros[indexCarro].consumo=0.4*equipas[indexEquipa].listaCarros[indexCarro].consumo;
         equipas[indexEquipa].listaCarros[indexCarro].tAvaria = avariaRec.tempo_parado;
       }

     }

     distPercorridaVolta=distPercorridaVolta+equipas[indexEquipa].listaCarros[indexCarro].velocidade;
     equipas[indexEquipa].listaCarros[indexCarro].combustivel=equipas[indexEquipa].listaCarros[indexCarro].combustivel-equipas[indexEquipa].listaCarros[indexCarro].consumo;
     if(equipas[indexEquipa].listaCarros[indexCarro].combustivel<=0){
       printf("O Carro %d ficou sem combustivel e desistiu da corrida\n", nCarro);
       sprintf(log, "CAR %d RAN OUT OF FUEL", nCarro);
       escreverLog(log);
       equipas[indexEquipa].listaCarros[indexCarro].voltasFinal=nVoltas;
       equipas[indexEquipa].listaCarros[indexCarro].estado_carro=STATE_DESISTENCIA;
       break;
     }

     if(distPercorridaVolta>=mem->configs[1]){
       if(mem->racefinish!=1){
         if (equipas[indexEquipa].listaCarros[indexCarro].estado_carro==STATE_SEGURANCA) {
           if(equipas[indexEquipa].estadoBox!=STATE_OCUPADA){
             equipas[indexEquipa].listaCarros[indexCarro].estado_carro=STATE_BOX;
             sprintf(log, "CAR %d IN BOX", nCarro);
             escreverLog(log);
             equipas[indexEquipa].estadoBox=STATE_OCUPADA;
             if(equipas[indexEquipa].listaCarros[indexCarro].tAvaria==0)
             sleep(2);
             else
             sleep(equipas[indexEquipa].listaCarros[indexCarro].tAvaria);
           }
           else{
             printf("Carro %d nao foi a box porque estava ocupada!\n", nCarro);
           }
         }
         if(equipas[indexEquipa].listaCarros[indexCarro].combustivel<combustivelPorVolta*3){
           printf("Carro %d tem combustivel apenas para 2 voltas\n", nCarro);
           equipas[indexEquipa].listaCarros[indexCarro].gota=1;
           if(equipas[indexEquipa].listaCarros[indexCarro].estado_carro==STATE_CORRIDA){
             equipas[indexEquipa].listaCarros[indexCarro].estado_carro=STATE_SEGURANCA;
             sprintf(log, "CAR %d IN SAFETY MODE", nCarro);
             escreverLog(log);
             equipas[indexEquipa].listaCarros[indexCarro].velocidade=0.3*equipas[indexEquipa].listaCarros[indexCarro].velocidade;
             equipas[indexEquipa].listaCarros[indexCarro].consumo=0.4*equipas[indexEquipa].listaCarros[indexCarro].consumo;
           }
           if(equipas[indexEquipa].estadoBox!=STATE_OCUPADA)
           equipas[indexEquipa].estadoBox=STATE_RESERVADA;
         }
         else if(equipas[indexEquipa].listaCarros[indexCarro].combustivel<combustivelPorVolta*5){
           printf("Carro %d tem combustivel apenas para 4 voltas\n", nCarro);
           equipas[indexEquipa].listaCarros[indexCarro].gota=1;
           if(equipas[indexEquipa].estadoBox==STATE_LIVRE){
             equipas[indexEquipa].listaCarros[indexCarro].estado_carro=STATE_BOX;
             sprintf(log, "CAR %d IN BOX", nCarro);
             escreverLog(log);
             equipas[indexEquipa].estadoBox=STATE_OCUPADA;
             sleep(2);
           }
           else{
             printf("Carro %d nao foi a box porquue estava ocupada!\n", nCarro);
           }
         }
         nVoltas++;
         if(nVoltas==mem->configs[2]){
           printf("O CARRO %d GANHOU A CORRIDA!!\n", nCarro);
           sprintf(log, "CAR %d WINS THE RACE", nCarro);
           escreverLog(log);
           mem->racefinish=1;
           equipas[indexEquipa].listaCarros[indexCarro].voltasFinal=nVoltas;
           break;
         }
         distPercorridaVolta=distPercorridaVolta-mem->configs[1];
         printf("Carro %d concluiu %d volta(s)  v:%.2lf  c:%.2lf\n",nCarro,nVoltas,equipas[indexEquipa].listaCarros[indexCarro].velocidade,equipas[indexEquipa].listaCarros[indexCarro].combustivel);
       }
       else{
         printf("O carro %d acabou a corrida com %d voltas\n", nCarro,nVoltas);
         equipas[indexEquipa].listaCarros[indexCarro].voltasFinal=nVoltas;
         break;
       }

     }
     pthread_mutex_unlock( & mem -> pmutex);
     sleep(1);
   }
}

int getIndexCarroAvariado(int teamID){
  for (size_t i = 0; i < mem->configs[4]; i++) {
    if(equipas[teamID].listaCarros[i].avariado==1){
      return i;
    }
  }
  return -1;
}

int getIndexCarroGota(int teamID){
  for (size_t i = 0; i < mem->configs[4]; i++) {
    if(equipas[teamID].listaCarros[i].gota==1){
      return i;
    }
  }
  return -1;
}



void * threadBox(void * threadid) // thread box que faz a gestao da box
{
  int teamID = * ((int * ) threadid);
  int indexCarroAvariado;
  int indexCarroGota;
  while (1) {
    while (equipas[teamID].estadoBox!=STATE_OCUPADA) {
      pthread_cond_wait(&mem->fullbox,&mem->pmutex);
    }
    indexCarroAvariado=getIndexCarroAvariado(teamID);
    if (indexCarroAvariado!=-1) {
      printf("Carro %d chegou a box da equipa %d\n",equipas[teamID].listaCarros[indexCarroAvariado].numero_carro,teamID+1);
      equipas[teamID].listaCarros[indexCarroAvariado].velocidade=equipas[teamID].listaCarros[indexCarroAvariado].velocidade/0.3;
      equipas[teamID].listaCarros[indexCarroAvariado].consumo=equipas[teamID].listaCarros[indexCarroAvariado].consumo/0.4;
      sleep(equipas[teamID].listaCarros[indexCarroAvariado].tAvaria);
      equipas[teamID].listaCarros[indexCarroAvariado].tAvaria=0;
      equipas[teamID].listaCarros[indexCarroAvariado].totalboxes++;
      equipas[teamID].listaCarros[indexCarroAvariado].avariado=0;
      equipas[teamID].estadoBox=STATE_LIVRE;
      printf("Carro %d acabou a reparacao e saiu da box\n", equipas[teamID].listaCarros[indexCarroAvariado].numero_carro);
    }
    else{
      printf("Carro %d chegou a box da equipa %d para meter combustivel\n",equipas[teamID].listaCarros[indexCarroGota].numero_carro,teamID+1);
      indexCarroGota=getIndexCarroGota(teamID);
      sleep(2);
      equipas[teamID].listaCarros[indexCarroGota].gota=0;
      equipas[teamID].listaCarros[indexCarroGota].totalboxes++;
      mem->totalAbastecimentos++;
      equipas[teamID].listaCarros[indexCarroGota].combustivel=mem->configs[6];
      equipas[teamID].listaCarros[indexCarroGota].estado_carro=STATE_CORRIDA;
      equipas[teamID].estadoBox=STATE_LIVRE;
      printf("Carro %d meteu combustivel e saiu da box da equipa %d\n",equipas[teamID].listaCarros[indexCarroGota].numero_carro,teamID+1);
    }
  }
}

void gestorEquipa(int n) { // gestor de equipas que cria as threads carro, um gestor de equipas por equipa.

   printf("[%d] Equipa %d\n", getpid(), n);
   int id[256];

   for (size_t i = 0; i < mem -> configs[4]; i++) {
      equipas[n].listaCarros[i].exist = 0;
      id[mem -> nCar] = mem -> nCar ;
      equipas[n].listaCarros[i].carIDD=mem->nCar;
      pthread_create( & equipas[n].listaCarros[i].threadCarro, NULL, threadCarro, & id[mem -> nCar]);
      mem -> nCar++;
      sem_wait(sem_car);
   }
   pthread_create( & equipas[n].threadBox, NULL, threadBox, &n);
   sem_post(semm);
   pthread_exit(NULL);
}

int checkEquipa(char * nomeEquipa) { // verifica se a equipa ja existe no programa e devolve o index da mesma no array de equipas
   int i = 0;
   for (i = 0; i < mem -> configs[3]; i++) {
      if (strcmp(nomeEquipa, equipas[i].nome) == 0) {
         return i;
      }
      if (strcmp(equipas[i].nome, "equipaVazio") == 0) {
          strcpy(equipas[i].nome, nomeEquipa);
         return i;
      }
   }
   return -1;
}

int checkCarro(int nCarro) { // verifica se o carro existe no programa
   for (size_t i = 0; i < mem -> configs[3]; i++) {
      for (size_t j = 0; j < equipas[i].nCarros; j++) {
         if (equipas[i].listaCarros[j].numero_carro == nCarro)
            return 0;
      }
   }
   return 1;
}

void gestorCorrida(int * a) { // gestor de corrida que cria os gestores de equipas
   printf("[%d] Gestor de Corrida\n", getpid());
   escreverLog("PROCESS RACE MANAGER CREATED");
   for (int i = 0; i < * (a + 3); i++) {
      sem_wait(semm); // semaforo que faz a espera da criacao da equipa
      if ((equipas[i].pidEquipa = fork()) == 0) {
         equipas[i].pidEquipa = getpid();
         equipas[i].estadoBox = STATE_LIVRE;
         equipas[i].nCarros = 0;
         strcpy(equipas[i].nome, "equipaVazio");
         gestorEquipa(i);
         exit(0);
      } else if (equipas[i].pidEquipa == -1) {
         perror("Falha na criação dos gestores de Equipas.\n");
         exit(1);
      }
   }
   sem_wait(semm);
   sem_post(semm);
   int fd;
   if ((fd = open(PIPE_NAME, O_RDWR)) < 0) {
      perror("Cannot open pipe for reading: ");
      exit(0);
   }

   char * comando;
   char * inputs;
   int percentagem_fiabilidade, numero_carro1;
   char * equipa;
   equipa = malloc(200 * sizeof(char));
   double consumo, velocidade;
   int equipaIndex;
   namedpipe namedpipe1;
   int raceConditionsOn=1;

   while (1) { // pipe fica a espera dos comandos no while. assim que recebe um comando fecha o semaforo e so o abre quando o comando recebido e executado na totalidade.

      read(fd, & namedpipe1, sizeof(namedpipe));
      strtok(namedpipe1.pipe_message, "\n");
      fseek(stdout, 0, SEEK_END);
      comando = strtok(namedpipe1.pipe_message, ":!");
      inputs = strtok(NULL, "");


      // CRIAR UM IF PARA CADA TIPO DE COMANDO QUE ESTA NO enunciado

      if (strcmp(comando, "START RACE") == 0) {
        if(mem->raceOn!=1){
          raceConditionsOn=1;
          for (int i = 0; i < mem -> configs[3]; i++) {
             for (int j = 0; j < mem -> configs[4]; j++) { // percorre todos os carros e caso esteja a funcionar cria uma avaria dependendo da sua fiabilidade
                if(equipas[i].listaCarros[j].exist==0){
                  raceConditionsOn=0;
                }
             }
          }
          if(raceConditionsOn){
            escreverLog("NEW COMMAND RECEIVED: START RACE");
            mem->raceOn=1;
            escreverLog("NOT ENOUGH CARS OR TEAMS");
            pthread_cond_broadcast(&mem->condvar);
            pthread_mutex_unlock(&mem->pmutex);
            sem_post(raceStart);
          }
          else {
            printf("Nao existem condicoes para o inicio da corrida.\n");
            escreverLog("ERROR STARTING RACE: NOT ENOUGH CARS OR TEAMS");
          }

        }
        sem_post(semm);

      } else if (strcmp(comando, "EXIT") == 0) {
         cleanup(SIGINT);

      } else if (strcmp(comando, "STOP") == 0) {
        if(mem->raceOn==1){
          mem->racefinish=1;
          escreverLog("NEW COMMAND RECEIVED: STOP");
        }
         else{
           printf("A corrida ainda nao comecou logo nao pode ser interrompida.\n");
           escreverLog("ERROR ON COMMMAND STOP");
         }
      } else if (strcmp(comando, "RESET") == 0) {
        for (size_t i = 0; i < mem->configs[3]; i++) {
          for (size_t j = 0; j < mem->configs[4]; j++) {
            printf("%d eheh\n", equipas[i].listaCarros[j].numero_carro);
          }
        }
      }
      else if (strcmp(comando, "ADDCAR TEAM") == 0) {
         sscanf(inputs, "%s CAR: %d, SPEED: %lf, CONSUMPTION: %lf, RELIABILITY: %d\n", equipa, & numero_carro1, & velocidade, & consumo, & percentagem_fiabilidade);
         strtok(equipa, ",");
         equipaIndex = checkEquipa(equipa);
         if (equipaIndex == -1)
            printf("Equipas cheias\n");
         else if (equipas[equipaIndex].nCarros < mem -> configs[4]) { // insere os dados do carro recebidos pelo comando do pipe
            if (checkCarro(numero_carro1)) {
                equipas[equipaIndex].listaCarros[equipas[equipaIndex].nCarros].numero_carro=numero_carro1;
                equipas[equipaIndex].listaCarros[equipas[equipaIndex].nCarros].velocidade=velocidade;
                equipas[equipaIndex].listaCarros[equipas[equipaIndex].nCarros].consumo=consumo;
                equipas[equipaIndex].listaCarros[equipas[equipaIndex].nCarros].percentagem_fiabilidade=percentagem_fiabilidade;
                equipas[equipaIndex].listaCarros[equipas[equipaIndex].nCarros].avariado=0;
                equipas[equipaIndex].listaCarros[equipas[equipaIndex].nCarros].exist=1;
               char str1[80];
               strcpy(str1, "NEW CAR LOADED -> TEAM:");
               strcat(str1, inputs);
               escreverLog(str1);
               printf("%s\n", str1);
               equipas[equipaIndex].nCarros++;
            } else
               printf("Ja existe um carro com o nr %d\n", numero_carro1);
         } else
            printf("Equipa %s esta preenchida.\n", equipas[equipaIndex].nome);

      } else {
         escreverLog("WRONG COMMAND");
         printf("Comando invalido -> %s -%s.\n", comando, inputs);
         sem_post(semm);
      }
   }

}
void gestorAvarias() {
   printf("[%d] Gestor de Avarias\n", getpid());
   escreverLog("PROCESS MALFUNCTION MANAGER CREATED");
   int k;
   sem_wait(raceStart);
   while (1) {
      sleep(mem -> configs[5]); // dorme de T_avaria em T_avaria
      for (size_t i = 0; i < mem -> configs[3]; i++) {
         for (size_t j = 0; j < mem -> configs[4]; j++) { // percorre todos os carros e caso esteja a funcionar cria uma avaria dependendo da sua fiabilidade
            if (equipas[i].listaCarros[j].avariado == 0 && equipas[i].listaCarros[j].estado_carro!=STATE_BOX) {
               k = rand() % equipas[i].listaCarros[j].percentagem_fiabilidade;
               if (k == 0) {
                 mem->totalAvarias++;
                  equipas[i].listaCarros[j].avariado = 1;
                  msgbuf avariaMessage;
                  avariaMessage.tempo_parado = rand() % 6 + 2;
                  avariaMessage.carNumber = equipas[i].listaCarros[j].numero_carro;
                  msgsnd(msqid, & avariaMessage, sizeof(avariaMessage), 0);
               }
            }
         }
      }
   }
}

void monitor() // função que apanha o sinal SIGINT e encaminha-o para a função cleanup() q faz a limpeza dos recursos
{

   struct sigaction act;
   act.sa_handler = cleanup;
   act.sa_flags = 0;
   if ((sigemptyset( & act.sa_mask) == -1) ||
      (sigaction(SIGINT, & act, NULL) == -1))
      perror("Failed to set SIGINT to handle Ctrl-C");
   while (1) {
      sleep(5);
   }
   exit(0);
}

int main(int argc, char
   const * argv[]) {
   FILE * fp;
   fp = fopen("log.txt", "w");
   fclose(fp);

   int * config; // Array com as configs. Está por ordem do enunciado, só ir ver
   config = readConfig();
   int numeroCarros= * (config + 4);
   int numeroEquipas= * (config + 3);
   if ((shmid = shmget(IPC_PRIVATE, sizeof(shared_mem) + sizeof(equipa) * numeroEquipas + numeroCarros*numeroEquipas*sizeof(carro), IPC_CREAT | 0777)) < 0) {
      perror("Erro no shmget com IPC_CREAT\n");
      exit(1);
   }
   if ((mem = (shared_mem * ) shmat(shmid, NULL, 0)) == (shared_mem * ) - 1) {
      perror("error in shmat");
      exit(1);
   }
   for (size_t i = 0; i < 7; i++) {
      mem -> configs[i] = * (config + i);
   }

   // criacao dos semaforos na shared mem
   sem_init( & mem -> sem_semm, 1, 1);
   semm = & mem -> sem_semm;
   sem_init( & mem -> sem_car, 1, 0);
   sem_car = & mem -> sem_car;
   sem_init( & mem -> sem_mutex, 1, 1);
   mutex = & mem -> sem_mutex;
   sem_init( & mem -> sem_raceStart, 1, 0);
   raceStart = & mem -> sem_raceStart;

   equipas = (equipa * )(mem + 1);
   carros = (carro*)(equipas+numeroEquipas);

   for (size_t i = 0; i < ( * (config + 3)); i++) {
      equipas[i].listaCarros = carros+(i*numeroCarros);
   }
   // criacao dos mutexes das threads
   mem -> nCar = 0;
   mem -> raceOn=0;
   mem->totalAvarias=0;
   mem->totalAbastecimentos=0;
   pthread_mutexattr_t mattr;
   pthread_mutexattr_setpshared( & mattr, PTHREAD_PROCESS_SHARED);
   pthread_mutex_init( & mem -> pmutex, & mattr);

   pthread_condattr_t cattr;
   pthread_condattr_setpshared( & cattr, PTHREAD_PROCESS_SHARED);
   pthread_condattr_t cattr2;
   pthread_condattr_setpshared( & cattr2, PTHREAD_PROCESS_SHARED);
   pthread_cond_init( & mem -> condvar, & cattr);
   pthread_cond_init( & mem -> fullbox, & cattr2);
   pthread_cond_init( & mem -> emptybox, & cattr);

   // criacao do message queue
   key = 1234;
   if ((msqid = msgget(key, IPC_CREAT | 0666)) < 0) {
      fprintf(stderr, "msgget error: %s\n", strerror(errno));
      exit(1);
   } else {
      printf("Message Queue criado com ID %d\n", msqid);
   }

   // criacao do named pipe
   if ((mkfifo("PIPE_NAME", O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)) {
      perror("Cannot create pipe: ");
      exit(0);
   }
   if ((gestor_de_corrida_id = fork()) == 0) {
      gestorCorrida(config);
      exit(0);
   }

   if ((gestor_de_avarias_id = fork()) == 0) {
      gestorAvarias();
      exit(0);
   }
   escreverLog("SIMULATOR STARTED");
   monitor();
   exit(0);
}
