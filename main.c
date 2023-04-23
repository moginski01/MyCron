#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <errno.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <spawn.h>
#include <time.h>
#include "log.h"

#define TASKS_LIMIT 100
int is_server_on = 0;

enum TASK_STATUS{
    ACTIVE,
    CYCLIC,
    DISABLED,
    CANCELLED
};

struct tasks{
    char *args[4];
    int id;//posłuży przy okazji jako indeks jaki jest obecnie(nie przewiduje cofania indeksu zawsze zwiększamy o jeden
    timer_t timer_id;
    struct sigevent sigevent;
    struct itimerspec value;
    enum TASK_STATUS status;
};

struct query_t {
    char txt[100];
    int option;//0-5
};

int current_task_index = 0;
struct tasks list_of_tasks[TASKS_LIMIT];

int string_to_int(const char* str) {
    return (int)strtol(str, NULL, 10);
}


void *task_realization(void *arg){

    struct tasks *res = (struct tasks*)arg;

    pid_t pid;
    char *argv[] = {"sh", "-c", res->args[3], NULL};
    posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, NULL);
    if(list_of_tasks[res->id].status==ACTIVE){
        list_of_tasks[res->id].status = DISABLED;//tylko w jednym miejscu w kodzie mamy dostęp do tego po utworzeniu timera i jest to tutaj
    }

    return NULL;
}

void free_memory(struct tasks *tasks,int how_many_to_delete){
    for(int i=0;i<how_many_to_delete;i++){
        for(int j=0;j<4;j++){
            free(tasks[i].args[j]);
        }
    }
}

void delete_timers(struct tasks *tasks,int how_many_to_delete){
    for(int i=0;i<how_many_to_delete;i++){
        if(tasks[i].status==ACTIVE || tasks[i].status==DISABLED){
            timer_delete(tasks[i].timer_id);
            tasks[i].status=CANCELLED;
        }
    }
}

void clear_querry(struct query_t *query){
    for(int i=0;i<(int)strlen(query->txt);i++){
        query->txt[i] = '\0';
    }
}

int main(int argc, char* argv[]) {
    //informacje o kolejce
    logger_init(MAX,SIGRTMIN,SIGRTMIN+1);
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct query_t);
    attr.mq_flags = 0;

    //do kolejki z listą zadań
    struct mq_attr attr_2;
    attr_2.mq_maxmsg = TASKS_LIMIT;//maksymalnie ilość tasków żeby była w kolejce
    attr_2.mq_msgsize = sizeof(struct query_t);
    attr_2.mq_flags = 0;




    mqd_t server;
    mqd_t server_exist = mq_open ("/mq_tasks_queue", O_CREAT | O_EXCL, 0666, &attr);
    if(server_exist==-1){
        if(errno==EEXIST){
            log(NORMAL,"Client %d",__LINE__);
            is_server_on = 1;
        }
        else{
            //log problem z otwarciam
            log(NORMAL,"Problem z otwarciem kolejki: %d",__LINE__);
            logger_destroy();
            return 1;
        }
    }
    else{
        //close the queue descriptor
        log(NORMAL,"Server %d",__LINE__);
        mq_close(server_exist);
    }


    if(is_server_on==1){
        //klient
        if(argc<4){
            //logger
            log(NORMAL,"Nieodpowiednia ilosc argumentow: %d",__LINE__);
            logger_destroy();
            return 2;
        }

        server = mq_open ("/mq_tasks_queue", O_WRONLY);
        if(server == -1){
            log(NORMAL,"Problem z otwarciem kolejki: %d",__LINE__);
            logger_destroy();
            return -2;
        }

        struct query_t query;

        strcpy(query.txt, argv[1]);//opcja co ma robić
        strcat(query.txt," ");
        strcat(query.txt, argv[2]);//czas sekundy
        strcat(query.txt," ");
        strcat(query.txt, argv[3]);//czas minuty
        strcat(query.txt," ");
        strcat(query.txt, argv[4]);//program i argumenty

        int send_test = mq_send(server,(char*)&query,sizeof(struct query_t),0);
        if(send_test==-1){
            log(NORMAL,"Problem z wyslaniem wiadomosci %d",__LINE__);
            mq_close(server);
            logger_destroy();
            return -3;
//            printf("%s\n",strerror(errno));
        }
        if(string_to_int(argv[1])==4){
            mqd_t queue_task_list = mq_open ("/mq_tasks_list_queue",O_CREAT | O_RDONLY, 0444, &attr_2);
            if(queue_task_list==-1){
                //logger
//                printf("Nie moge otworzyc\n");
                log(NORMAL,"Problem z otwarciem kolejki: %d",__LINE__);

                mq_close(server);
                logger_destroy();
                return -2;
            }

            struct query_t result;
            int test = mq_receive(queue_task_list,(char *)&result,sizeof(struct query_t),NULL);
            if(test==-1){
//                printf("%s\n",strerror(errno));
                log(NORMAL,"Problem otrzymaniem listy zadan %d",__LINE__);

                mq_close(queue_task_list);
                mq_close(server);
                logger_destroy();
                return -2;
            }
            int how_many_tasks = string_to_int(result.txt);
            for(int i=0;i<how_many_tasks;i++){
                clear_querry(&query);
                test = mq_receive(queue_task_list,(char *)&result,sizeof(struct query_t),NULL);
                if(test==-1){
                    log(NORMAL,"Problem otrzymaniem listy zadan %d",__LINE__);

                    mq_close(queue_task_list);
                    mq_close(server);
                    logger_destroy();
                    return -2;
                }
                printf("%s\n",result.txt);
            }
            mq_close(queue_task_list);
        }
        mq_close(server);


    }
    else{
        //serwer
        server = mq_open ("/mq_tasks_queue", O_RDONLY);
        if(server==-1){
            //logger problem z otworzeniem kolejki
            log(NORMAL,"Problem z otworzeniem kolejki: %d",__LINE__);
            logger_destroy();
            return -1;
        }

        struct query_t query;
        query.txt[0] = '\0';

        while(1){

            int test = mq_receive (server, (char*)&query, sizeof(struct query_t), 0);
            if(test==-1){
                //logger probleml
                log(NORMAL,"Problem z odbiorem danych z kolejki: %d",__LINE__);

                delete_timers(list_of_tasks,current_task_index);
                free_memory(list_of_tasks,current_task_index);
                mq_unlink("/mq_tasks_queue");
                mq_unlink("/mq_tasks_list_queue");
                logger_destroy();
                return 1;
            }
            if(current_task_index==TASKS_LIMIT){
                //log że nie można robić nowych tasków
                log(NORMAL,"Osiagnieto limit zadan: %d",__LINE__);

                continue;
            }
//            printf("Wiadomosc: %s\n",query.txt);
            list_of_tasks[current_task_index].id = current_task_index;

            char *word;
            word = strtok (query.txt," ");
            if(string_to_int(word)==0){//natychmiast mamy wykonać zadanie
                list_of_tasks[current_task_index].args[0] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[0]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[0],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[1] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[1]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;
                }

                strcpy(list_of_tasks[current_task_index].args[1],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[2] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[2]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[2],word);

                word = strtok (NULL,"");

                list_of_tasks[current_task_index].args[3] = malloc(sizeof(char)*strlen(word+4));
                if(list_of_tasks[current_task_index].args[3]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[3],word);

                if(strcmp(word, "close_server") == 0){
                    log(NORMAL,"Zamkniecie serwera: %d",__LINE__);

                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    free(list_of_tasks[current_task_index].args[3]);
                    mq_close(server);
                    delete_timers(list_of_tasks,current_task_index);
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    //destroy tu nie ma bo break jest na koniec programu
                    break;

                }
                list_of_tasks[current_task_index].status=DISABLED;//od razu disabled bo wykona sie natychmiast


                list_of_tasks[current_task_index].sigevent.sigev_notify = SIGEV_THREAD;
                list_of_tasks[current_task_index].sigevent.sigev_notify_function = task_realization;
                list_of_tasks[current_task_index].sigevent.sigev_value.sival_ptr = (void*)&list_of_tasks[current_task_index];
                list_of_tasks[current_task_index].sigevent.sigev_notify_attributes = NULL;
                int res = timer_create(CLOCK_REALTIME,&list_of_tasks[current_task_index].sigevent,&list_of_tasks[current_task_index].timer_id);

                list_of_tasks[current_task_index].value.it_value.tv_sec = 0;
                list_of_tasks[current_task_index].value.it_value.tv_nsec = 1;
                list_of_tasks[current_task_index].value.it_interval.tv_sec = 0;
                list_of_tasks[current_task_index].value.it_interval.tv_nsec = 0;
                timer_settime(list_of_tasks[current_task_index].timer_id,0,&list_of_tasks[current_task_index].value,NULL);
            }else if(string_to_int(word)==1){//czas abs

                list_of_tasks[current_task_index].args[0] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[0]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[0],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[1] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[1]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;
                }

                strcpy(list_of_tasks[current_task_index].args[1],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[2] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[2]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[2],word);

                word = strtok (NULL,"");

                list_of_tasks[current_task_index].args[3] = malloc(sizeof(char)*strlen(word+4));
                if(list_of_tasks[current_task_index].args[3]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[3],word);

                if(strcmp(word, "close_server") == 0){
                    log(NORMAL,"Zamkniecie serwera: %d",__LINE__);

                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    free(list_of_tasks[current_task_index].args[3]);
                    mq_close(server);
                    delete_timers(list_of_tasks,current_task_index);
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    //destroy tu nie ma bo break jest na koniec programu
                    break;

                }
                list_of_tasks[current_task_index].status = ACTIVE;

                list_of_tasks[current_task_index].sigevent.sigev_notify = SIGEV_THREAD;
                list_of_tasks[current_task_index].sigevent.sigev_notify_function = task_realization;
                list_of_tasks[current_task_index].sigevent.sigev_value.sival_ptr = (void*)&list_of_tasks[current_task_index];
                list_of_tasks[current_task_index].sigevent.sigev_notify_attributes = NULL;
                int res = timer_create(CLOCK_REALTIME,&list_of_tasks[current_task_index].sigevent,&list_of_tasks[current_task_index].timer_id);

                list_of_tasks[current_task_index].value.it_value.tv_sec = string_to_int(list_of_tasks[current_task_index].args[1])*60 + string_to_int(list_of_tasks[current_task_index].args[2]);
                list_of_tasks[current_task_index].value.it_value.tv_nsec = 0;
                list_of_tasks[current_task_index].value.it_interval.tv_sec = 0;
                list_of_tasks[current_task_index].value.it_interval.tv_nsec = 0;
                timer_settime(list_of_tasks[current_task_index].timer_id,0,&list_of_tasks[current_task_index].value,NULL);
            }else if(string_to_int(word)==2){//czas względny
                list_of_tasks[current_task_index].args[0] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[0]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[0],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[1] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[1]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;
                }

                strcpy(list_of_tasks[current_task_index].args[1],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[2] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[2]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[2],word);

                word = strtok (NULL,"");

                list_of_tasks[current_task_index].args[3] = malloc(sizeof(char)*strlen(word+4));
                if(list_of_tasks[current_task_index].args[3]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[3],word);

                if(strcmp(word, "close_server") == 0){
                    log(NORMAL,"Zamkniecie serwera: %d",__LINE__);

                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    free(list_of_tasks[current_task_index].args[3]);
                    mq_close(server);
                    delete_timers(list_of_tasks,current_task_index);
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    //destroy tu nie ma bo break jest na koniec programu
                    break;

                }

                list_of_tasks[current_task_index].status = ACTIVE;

                list_of_tasks[current_task_index].sigevent.sigev_notify = SIGEV_THREAD;
                list_of_tasks[current_task_index].sigevent.sigev_notify_function = task_realization;
                list_of_tasks[current_task_index].sigevent.sigev_value.sival_ptr = (void*)&list_of_tasks[current_task_index];
                list_of_tasks[current_task_index].sigevent.sigev_notify_attributes = NULL;
                int res = timer_create(CLOCK_REALTIME,&list_of_tasks[current_task_index].sigevent,&list_of_tasks[current_task_index].timer_id);

                struct timespec current_time;
                clock_gettime(CLOCK_REALTIME, &current_time);

                list_of_tasks[current_task_index].value.it_value.tv_sec = current_time.tv_sec + string_to_int(list_of_tasks[current_task_index].args[1])*60 + string_to_int(list_of_tasks[current_task_index].args[2]);
                list_of_tasks[current_task_index].value.it_value.tv_nsec = 0;
                list_of_tasks[current_task_index].value.it_interval.tv_sec = 0;
                list_of_tasks[current_task_index].value.it_interval.tv_nsec = 0;
                timer_settime(list_of_tasks[current_task_index].timer_id,TIMER_ABSTIME,&list_of_tasks[current_task_index].value,NULL);
            }else if(string_to_int(word)==3){//cykliczne zadanie

                list_of_tasks[current_task_index].args[0] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[0]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[0],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[1] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[1]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;
                }

                strcpy(list_of_tasks[current_task_index].args[1],word);

                word = strtok (NULL," ");
                list_of_tasks[current_task_index].args[2] = malloc(sizeof(char)*strlen(word));
                if(list_of_tasks[current_task_index].args[2]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[2],word);

                word = strtok (NULL,"");

                list_of_tasks[current_task_index].args[3] = malloc(sizeof(char)*strlen(word+4));
                if(list_of_tasks[current_task_index].args[3]==NULL){
                    log(NORMAL,"Problem z alokacja pamieci: %d",__LINE__);
                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    delete_timers(list_of_tasks,current_task_index);//bo usuwa tylko do obecnego nie włącznie
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -8;

                }
                strcpy(list_of_tasks[current_task_index].args[3],word);

                if(strcmp(word, "close_server") == 0){
                    log(NORMAL,"Zamkniecie serwera: %d",__LINE__);

                    free(list_of_tasks[current_task_index].args[0]);
                    free(list_of_tasks[current_task_index].args[1]);
                    free(list_of_tasks[current_task_index].args[2]);
                    free(list_of_tasks[current_task_index].args[3]);
                    mq_close(server);
                    delete_timers(list_of_tasks,current_task_index);
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    //destroy tu nie ma bo break jest na koniec programu
                    break;

                }

                list_of_tasks[current_task_index].status = CYCLIC;

                list_of_tasks[current_task_index].sigevent.sigev_notify = SIGEV_THREAD;
                list_of_tasks[current_task_index].sigevent.sigev_notify_function = task_realization;
                list_of_tasks[current_task_index].sigevent.sigev_value.sival_ptr = (void*)&list_of_tasks[current_task_index];
                list_of_tasks[current_task_index].sigevent.sigev_notify_attributes = NULL;
                int res = timer_create(CLOCK_REALTIME,&list_of_tasks[current_task_index].sigevent,&list_of_tasks[current_task_index].timer_id);


                list_of_tasks[current_task_index].value.it_value.tv_sec = string_to_int(list_of_tasks[current_task_index].args[1])*60 + string_to_int(list_of_tasks[current_task_index].args[2]);
                list_of_tasks[current_task_index].value.it_value.tv_nsec = 0;
                list_of_tasks[current_task_index].value.it_interval.tv_sec = string_to_int(list_of_tasks[current_task_index].args[1])*60 + string_to_int(list_of_tasks[current_task_index].args[2]);
                list_of_tasks[current_task_index].value.it_interval.tv_nsec = 0;
                timer_settime(list_of_tasks[current_task_index].timer_id,0,&list_of_tasks[current_task_index].value,NULL);
            }else if (string_to_int(word)==4){//wysłanie listy zadań aktywnych do klienta
                mqd_t queue_to_client = mq_open ("/mq_tasks_list_queue",O_CREAT | O_WRONLY, 0666, &attr_2);//bylo 0644
                if(queue_to_client==-1){
                    //logger problem z utworzeniem kolejki do klienta
                    return -1;
                }

                struct query_t query1;
                sprintf(query1.txt,"%d",current_task_index);//przesłanie ile tasków
                int status = mq_send(queue_to_client,(char*)&query1,sizeof(struct query_t),0);
                if(status==-1){
                    log(NORMAL,"Problem z wyslaniem wiadomosci: %d",__LINE__);
                    mq_close(queue_to_client);

                    mq_close(server);
                    delete_timers(list_of_tasks,current_task_index);
                    free_memory(list_of_tasks,current_task_index);
                    mq_unlink("/mq_tasks_queue");
                    mq_unlink("/mq_tasks_list_queue");
                    logger_destroy();
                    return -4;
                }

                for(int i=0;i<current_task_index;i++){
                    if(list_of_tasks[current_task_index].status==DISABLED){
                        continue;
                    }
                    clear_querry(&query1);
                    sprintf(query1.txt,"ID: %d ",i);
                    for(int j=0;j<4;j++){
                        strcat(query1.txt,list_of_tasks[i].args[j]);
                        strcat(query1.txt," ");
                    }
                    status = mq_send(queue_to_client,(char*)&query1,sizeof(struct query_t),0);
                    if(status==-1){
                        log(NORMAL,"Problem z wyslaniem wiadomosci: %d",__LINE__);
                        mq_close(queue_to_client);

                        mq_close(server);
                        delete_timers(list_of_tasks,current_task_index);
                        free_memory(list_of_tasks,current_task_index);
                        mq_unlink("/mq_tasks_queue");
                        mq_unlink("/mq_tasks_list_queue");
                        logger_destroy();
                        return -4;
                    }
                }

                mq_close(queue_to_client);
                continue;
            }else if(string_to_int(word)==5){

                word = strtok (NULL," ");
                word = strtok (NULL," ");
                word = strtok (NULL,"");
                int id = string_to_int(word);
                if(list_of_tasks[id].status==ACTIVE || list_of_tasks[id].status==CYCLIC){
                    list_of_tasks[id].status=CANCELLED;
                    timer_delete(list_of_tasks[id].timer_id);

                }
                continue;
            }else{
                //log wrong number
                log(NORMAL,"Przekazano złe argumenty: %d",__LINE__);
                continue;
            }

            current_task_index++;

        }
        mq_unlink("/mq_tasks_list_queue");

    }

    logger_destroy();

    return 0;
}