#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdbool.h>

#define MSG_SIZE 4
//0 - Numer traktu
//1 - Znacznik czasowy
//2 - Id procesu
//3 - Typ informacji

//Typy informacji
#define ACCEPT 10
#define REFUSE 11
#define FREE 12
#define ASK_FOR_SPACE 14

#define MSG_TAG_TOKEN 10
#define TOKEN_MAX_VALUE 10
#define NUMBER_OF_LEGIONS 4
#define NUMBER_OF_ROADS 2

#define ROAD_NUMBER 0
#define TIME_STAMP 1
#define SENDER_ID 2
#define MESSAGE_TYPE 3

//Zmienna globalna - współdzielona
MPI_Status status;
int message[MSG_SIZE];
int legions[NUMBER_OF_LEGIONS]; //Tablica z odpowiedziami na broadcast
int roads[NUMBER_OF_ROADS]; //Rozmiary traktów

int myRoadNumber;
int myTimeStamp;
int myId;
int size;

int recivedMessages;
bool recivedAllMessages = false;
bool isMoreSpace = false;
bool imOnRoad = false;
bool broadcastSend = false;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexSpace = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t condSpace = PTHREAD_COND_INITIALIZER;


#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"    
#define KMAG  "\x1B[35m"    
#define KCYN  "\x1B[36m"    
#define KWHT  "\x1B[37m"    


/**
 * Zwraca 0 jeżeli należ potwiedzenie
 * Zwraca 1 jeżeli nalez sprzeciw
**/
int recivedAskForSpace(int* message){
    int roadNumber = message[ROAD_NUMBER];
    if(myRoadNumber == roadNumber){
        //Proces ubiega się o ten sam trakt 
        int timeStamp = message[TIME_STAMP];
	if(imOnRoad){
	    //jestem na trakcie - należy odesłać sprzeciw
            //printf(KRED "%d: SPRZECIW - jestem na trakcie %d\n", myId, myRoadNumber);
            return 1;  
	}
        if(myTimeStamp < timeStamp){
            //Znacznik czasowy tego procesu jest mniejszy, nalezy odesłać sprzeciw
            //printf(KRED "%d: SPRZECIW - lepszy znacznik czasowy niz %d\n", myId, message[SENDER_ID]);
            return 1;  
        } else if(myTimeStamp == timeStamp){
            //Równy piorytet - należy sprawdzić id procesu, aby wybrać lepszy
            int senderId = message[SENDER_ID];
            if(myId < senderId){
                //Id tego procesu jest lepsze, nalezy odesłac sprzeciw
            	//printf(KRED "%d: SPRZECIW - lepsze id niz %d\n", myId, message[SENDER_ID]);
                return 1;
            } else {
                //Proces ma lepsze id, nigdy nie będą równe
            	//printf(KBLU "%d: OK - gorsze id niz %d\n", myId, message[SENDER_ID]);
                return 0;
            }        
        } else {
            //Odebrany znacznik czasowy jest mniejszy niż mój
            //printf(KBLU "%d: OK - gorszy znacznik czasowy niz %d\n", myId, message[SENDER_ID]);
            return 0;
        }        
    } else {
        //Proces ubiega się o inny trakt
	//printf(KBLU "%d: OK - inny trakt niz %d\n", myId, message[SENDER_ID]);
        return 0;
    }
}


void acceptMessage(int* message){
    int reciver = message[SENDER_ID];

    int newMessage[MSG_SIZE];
    newMessage[ROAD_NUMBER] = myRoadNumber;
    newMessage[TIME_STAMP] = myTimeStamp;
    newMessage[SENDER_ID] = myId;
    newMessage[MESSAGE_TYPE] = ACCEPT;

    MPI_Send(newMessage, MSG_SIZE, MPI_INT, reciver, MSG_TAG_TOKEN, MPI_COMM_WORLD);    
}

void refuseMessage(int* message){

    int reciver = message[SENDER_ID];

    int newMessage[MSG_SIZE];
    newMessage[ROAD_NUMBER] = myRoadNumber;
    newMessage[TIME_STAMP] = myTimeStamp;
    newMessage[SENDER_ID] = myId;
    newMessage[MESSAGE_TYPE] = REFUSE;

    MPI_Send(newMessage, MSG_SIZE, MPI_INT, reciver, MSG_TAG_TOKEN, MPI_COMM_WORLD);
}


void *messageReciver(void *message_c){
    int *message = (int*)message_c;
    int senderId; 
    while(1){
        MPI_Recv(message, MSG_SIZE, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        switch(message[MESSAGE_TYPE]){
            case ACCEPT:
                senderId = message[SENDER_ID];
                legions[senderId] = 0;
                recivedMessages++;
            break;
            case REFUSE:
                //Proces wysyłający wiadomość odmawia zajęcia traktu - jest szybciej w kolejce do niego
                senderId = message[SENDER_ID];
                legions[senderId] = 1;
                recivedMessages++;
            break;
            case FREE:
                //Proces wysyłający wiadomość zwolnił miejsce na trakcie
                //Należy usunąć go z listy procesów zajmujących / oczekujących na trakt, jeżeli ten proces czeka na ten trakt
            
 		senderId = message[SENDER_ID];
                int senderRoadNumber = message[ROAD_NUMBER];
            	//printf(KNRM "%d: odebralem informacje o zwolnieniu miejsca od %d na trakcie %d\n", myId, senderId, senderRoadNumber);   
                if(senderRoadNumber == myRoadNumber){
                    legions[senderId] = 0;
                }                
                pthread_mutex_lock(&mutexSpace); 
		isMoreSpace = true;               
                pthread_cond_signal(&condSpace);    
                pthread_mutex_unlock(&mutexSpace); 
            break;
            case ASK_FOR_SPACE:
                if(recivedAskForSpace(message) == 0){
                    //Odesłanie potwiedzenia
                    acceptMessage(message);
                } else {
                    //odesłanie sprzeciwu
                    refuseMessage(message);
                }
            break;
            default:
            break;
        }        
        pthread_mutex_lock(&mutex);
        if(broadcastSend && recivedMessages == size - 1){
            //printf(KNRM "%d: odebralem wszystkie wiadomosci\n", myId);
            recivedAllMessages = true;
            pthread_cond_signal(&cond);
        }        
        pthread_mutex_unlock(&mutex); 
    }     
    return NULL;
}
       

void localSection(int maxWaitTime){   
    int waitTime = rand() % (maxWaitTime + 1);
    //printf(KGRN "%d: oczekuje na rozkazy w sekcji lokalnej %d sekund\n", myId, waitTime);
    sleep(waitTime);
}

void criticalSection(int maxWaitTime){
    imOnRoad = true;
    int waitTime = 5 + rand() % (maxWaitTime + 1);
    printf(KGRN "%d: Przechodzę przez trakt %d przez %d sekund\n", myId, myRoadNumber, waitTime);
    sleep(waitTime);
    imOnRoad = false;
}

void askForSpace(){
    int newMessage[MSG_SIZE];

    //Zwiększam swój znacznik czasowy - wysyłam broadcast
    myTimeStamp++;
    broadcastSend = true;

    
    newMessage[ROAD_NUMBER] = myRoadNumber;
    newMessage[TIME_STAMP] = myTimeStamp;
    newMessage[SENDER_ID] = myId;
    newMessage[MESSAGE_TYPE] = ASK_FOR_SPACE;

    for(int i = 0; i < size; i++){
        if(i != myId){
            MPI_Send(newMessage, MSG_SIZE, MPI_INT, i, MSG_TAG_TOKEN, MPI_COMM_WORLD);
        }
    }
    //printf(KYEL "%d: rozeslalem broadcast\n", myId);
}

void freeSpace(){
    int newMessage[MSG_SIZE];

    newMessage[SENDER_ID] = myId;
    newMessage[ROAD_NUMBER] = myRoadNumber;
    newMessage[TIME_STAMP] = myTimeStamp;
    newMessage[MESSAGE_TYPE] = FREE;
    
    for(int i = 0; i < size; i++){
        if(i != myId){
            MPI_Send(newMessage, MSG_SIZE, MPI_INT, i, MSG_TAG_TOKEN, MPI_COMM_WORLD);
        }
    }

    myRoadNumber = -1;
}

int main(int argc, char **argv){

    
    int required = 3;
    int provided;

    MPI_Init_thread(&argc, &argv, required, &provided);




	MPI_Comm_rank(MPI_COMM_WORLD, &myId);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if(myId == 0){
        printf(KNRM "%d: otrzymany poziom wspolbierznosci: %d\n", myId, provided);
    }

    srand(time(NULL) + myId);

    int roadsSize[] = {
        100,
        95
    };

    int legionsSize[] = {
        90,
        50,
        70,
        80
    };

    myRoadNumber = -1;

/*
    int roadsSize[] = {
        100,
        120,
        130,
        90,
        70,
        110
    }

    int legionsSize[] = {
        65,
        20,
        15,
        5,
        58,
        35,
        45,
        69,
        54,
        43,
        21,
        55,
        60,
        60,
        60,
        60,
        60,
        60,
        60
    }
*/

    //Liczba odbytych marszy po traktach
    int numberOfPasses = 0;

    //Maksymalna liczba marszy
    int maxNumberOfPasses = 100;
    
    //Maksymalny czas oczekiwania na rozkazy (w sekundach)
    int maxWaitTime = 2;

    //Maksynalny czas w sekcji krytycznej
    int maxWaitTimeCritical = 10;


    pthread_t t1;
    if(pthread_create(&t1, NULL, messageReciver, &message)){
        perror("Blad: Tworzenie watku! \n");
    }

    //Petla główna
    while(numberOfPasses < maxNumberOfPasses){
        localSection(maxWaitTime);

        //Losowy wybór traktu
        myRoadNumber = rand() % NUMBER_OF_ROADS;
        printf(KNRM "%d: wylosowalem trakt: %d\n", myId, myRoadNumber);

        recivedMessages = 0;
        recivedAllMessages = false;
        isMoreSpace = false;
        //printf(KYEL "%d: rozsylam broadcast\n", myId);
        askForSpace(myRoadNumber);

        pthread_mutex_lock(&mutex);
        while(!recivedAllMessages){
            //Oczekiwanie na odpowiedź od wszystkich procesów
            //printf(KNRM "%d: czekam na odpowiedz od wszystkich\n", myId);
            pthread_cond_wait(&cond, &mutex);
        }	
        //printf(KNRM "%d: sprawdzam miejsce na trakcie\n", myId);
        pthread_mutex_unlock(&mutex);

        broadcastSend = false;
        //Sprawdzenie ile miejsca jest zajętego na trakcie
        int numberOfLegionists = 0;
        for(int i = 0; i < size; i++){
            if(i != myId){
                numberOfLegionists += legions[i] * legionsSize[i];
            }
        }

        //Sprawdzenie czy jest miejsce na trakcie
        while(roadsSize[myRoadNumber] < numberOfLegionists + legionsSize[myId]){
            //printf(KNRM "%d: moj rozmiar: %d\n", myId, legionsSize[myId]);
            //printf(KNRM "%d: rozmiar traktu: %d\n", myId, roadsSize[myRoadNumber]);
            //printf(KNRM "%d: zajete miejsce: %d\n", myId, numberOfLegionists);
            pthread_mutex_lock(&mutexSpace);
            while(!isMoreSpace){
		//printf(KNRM "%d: czekam na zwolnienie miejsca na trakcie %d\n", myId, myRoadNumber);
                //Oczekiwanie na zwolnienie miejsca
                pthread_cond_wait(&condSpace, &mutexSpace);
            }
	    //printf(KYEL "%d: odnotowalem zwolnienie miejsca na trakcie: %d\n", myId, myRoadNumber);
            isMoreSpace = false;
            pthread_mutex_unlock(&mutexSpace);

            numberOfLegionists = 0;
            for(int i = 0; i < size; i++){
                if(i != myId){
                numberOfLegionists += legions[i] * legionsSize[i];
                }
            }
        }
	    printf(KBLU "%d: wchodze na trakt\n", myId);
        //Wchodzi na trakt
        criticalSection(maxWaitTimeCritical);
        //Broadcast o zwolnieniu miejsca
        freeSpace();
        printf(KYEL "%d: zwalniam miejsce na trakcie\n", myId);

    }


    printf(KNRM "%d: Koncze dzialanie\n", myId);
    
		
	MPI_Finalize();
}
