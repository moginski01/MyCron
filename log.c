//
// Created by root on 1/25/23.
//

#include "log.h"

#define MAX_MSG_SIZE 200

volatile sig_atomic_t logger_detail_level = 0;
static struct data{
    int signal_1;
    int signal_2;
    sem_t sem_dump;
    sem_t sem_log;
    struct sigaction act_log;
    struct sigaction act_dump;
    char *log_file;
    pthread_t thread_dump;
}log_data;


//0 - off
//1 - MIN
//2 - STANDARD
//3 - MAX



void signal_1_handler(int sig, siginfo_t* info, void* context){
    int data = info->si_value.sival_int;
//    printf("Sig1 handler %d\n",data);
    atomic_store(&logger_detail_level,data);
//    sem_post(&log_data.sem_log);
}

void signal_2_handler(){
//    printf("Sig2 handler\n");
    sem_post(&log_data.sem_dump);
}

char* create_name_from_date() {
    char *log_filename = (char*)calloc(100, sizeof(char));
    if (log_filename == NULL) {
        return NULL;
    }

    time_t now = time(0);
    struct tm time = *localtime(&now);

    //dodajemy 1900 i 1 ponieważ struktura odlicza czas od 1900 roku
    sprintf(log_filename, "log_%d_%d_%d_%d_%d_%d.txt", time.tm_mday,
            time.tm_mon+1, time.tm_year + 1900, time.tm_hour, time.tm_min, time.tm_sec);


//    printf("%s\n",log_filename);

    return log_filename;
}

int logger_init(int logging_level, int sig_no_1, int sig_no_2){
    if(logger_detail_level!=0){
        return 1;
    }

    log_data.signal_1 = sig_no_1;
    log_data.signal_2 = sig_no_2;

    atomic_store(&logger_detail_level, logging_level);
    sem_init(&log_data.sem_log,0,1);
    sem_init(&log_data.sem_dump,0,0);

    //obsługa sygnałów
    //instalacja handlerów
    //uruchoimenie wątku dump
    //opcjonalnie wyłączyć buforowanie dla zapisu
    //semafory pootwierać
    //

    //sigaction
    sigset_t set;
    sigfillset(&set);

    log_data.act_log.sa_sigaction = signal_1_handler;
    log_data.act_log.sa_mask = set;
    log_data.act_log.sa_flags = SA_SIGINFO;

    sigaction(sig_no_1, &log_data.act_log, NULL);

    pthread_create(&log_data.thread_dump,NULL,dump,NULL);

//    sigaction(sig_no_2, &act, NULL);
    sigset_t set2;
    sigfillset(&set2);

    log_data.act_log.sa_sigaction = signal_2_handler;
    log_data.act_log.sa_mask = set2;
    log_data.act_log.sa_flags = SA_SIGINFO;

    sigaction(sig_no_2, &log_data.act_log, NULL);


    //utworzenie nazwy pliku log
    log_data.log_file = create_name_from_date();

    return 0;
}

void *dump(void *arg){
    while(1){
        sem_wait(&log_data.sem_dump);

        time_t now = time(0);
        struct tm time = *localtime(&now);
        char *dump_file = calloc(100,sizeof(char));
        if(dump_file==NULL){
            log(HIGH,"Failed to allocate memory for dump_filename %d",__LINE__);
            return NULL;
        }

        sprintf(dump_file,"dumpfile_%d_%d_%d_%d_%d_%d.bin",time.tm_mday,
                time.tm_mon+1, time.tm_year + 1900, time.tm_hour, time.tm_min, time.tm_sec);

        FILE *f = fopen(dump_file,"wb");
        if(f==NULL){
            log(HIGH,"Failed to create dump file %d",__LINE__);
            return NULL;
        }
//        printf("FUNKCJA WATEK DUMP\n");
//        (int detail_level, int sig_no_1, int sig_no_2)
        fwrite(&logger_detail_level,sizeof(logger_detail_level),1,f);
        fwrite(&log_data.signal_1,sizeof(log_data.signal_1),1,f);
        fwrite(&log_data.signal_2,sizeof(log_data.signal_2),1,f);

        //dodajemy 1900 i 1 ponieważ struktura odlicza czas od 1900 roku





        free(dump_file);
        fclose(f);

//        sem_post(&sem_dump);
    }
    return NULL;
}

int log(int level, char *msg,...){

    if(level>logger_detail_level){
        return 1;
    }
    sem_wait(&log_data.sem_log);

    //
    //do pliku pisanie
    va_list args;
    char *buff= malloc(sizeof(char)*MAX_MSG_SIZE);
    if(buff==NULL){
        return 2;
    }
    va_start(args,msg);
    vsnprintf(buff, MAX_MSG_SIZE,msg,args);
    FILE *f = fopen(log_data.log_file,"a+");
    if(f==NULL){
        free(buff);
        return 3;
    }
    time_t now = time(0);
    struct tm time = *localtime(&now);

    //dodajemy 1900 i 1 ponieważ struktura odlicza czas od 1900 roku
    fprintf(f,"%d_%d_%d_%d_%d_%d %s\n",time.tm_mday,
            time.tm_mon+1, time.tm_year + 1900, time.tm_hour, time.tm_min, time.tm_sec,buff);

//    fprintf(f,"%s\n", buff);
//    printf("%s\n",buff);
    va_end(args);
    free(buff);
    fclose(f);

    sem_post(&log_data.sem_log);

    return 0;
}


void logger_destroy(){
    //zamykamy wątki
    //oryginalna akcja dla sygnałow
    //usunąć handlery?
    //semafory pousuwać
    //zwalniać pamięć jak coś będzie


    log(NORMAL,"Closing log %d",__LINE__);
    atomic_store(&logger_detail_level,0);
    sem_destroy(&log_data.sem_log);
    pthread_cancel(log_data.thread_dump);
    sem_destroy(&log_data.sem_dump);
    free(log_data.log_file);

}

void send_signal(int pid, int signo, int value) {
    pid_t p_id = pid;
    union sigval val;
    val.sival_int = value;
    int res = sigqueue(p_id,signo,val);
    if(res !=0){
        printf("Error z wysłaniem sygnału");
    }

}

//option 0-po prostu wyświetlenie zawartości
//option 1-zrobienie inita dla zawartości
int load_dump(char *dump_filename,int option){
    //nie ma komunikatów do loga ponieważ zakładam że funkcja jest wykorzystywana przed użyciem inita

    if(option==0){
        FILE *f= fopen(dump_filename,"rb");
        if(f==NULL){
            return 2;
        }

        sig_atomic_t temp_val1;
        int sig1,sig2;
        fread(&temp_val1,sizeof(temp_val1),1,f);
        fread(&sig1,sizeof(sig1),1,f);
        fread(&sig2,sizeof(sig2),1,f);

        printf("Wczytano:\n");
        printf("log_level: %d\n",temp_val1);
        printf("Wczytano: %d\n",sig1);
        printf("Wczytano:%d\n",sig2);

        fclose(f);
    }else if(option==1){
        if(logger_detail_level!=0){
            return 1;
        }
        FILE *f= fopen(dump_filename,"rb");
        if(f==NULL){
            return 2;
        }

        sig_atomic_t temp_val1;
        int sig1,sig2;
        fread(&temp_val1,sizeof(temp_val1),1,f);
        fread(&sig1,sizeof(sig1),1,f);
        fread(&sig2,sizeof(sig2),1,f);

        printf("Wczytano:\n");
        printf("log_level: %d\n",temp_val1);
        printf("Wczytano: %d\n",sig1);
        printf("Wczytano:%d\n",sig2);
        int res = logger_init(temp_val1,sig1,sig2);
        fclose(f);
        return res;
    }else{
        return 3;
    }


    return 0;
}