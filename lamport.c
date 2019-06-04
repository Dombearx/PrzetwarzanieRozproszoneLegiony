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
#define NUMBER_OF_LEGIONS 5
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
 * Zwraca 0 jeżeli należy odesłać potwiedzenie
 * Zwraca 1 jeżeli nalezy odesłac sprzeciw
**/
int recivedAskForSpace(int* message){
    int roadNumber = message[ROAD_NUMBER];
    if(myRoadNumber == roadNumber){
        //Proces ubiega się o ten sam trakt 
        int timeStamp = message[TIME_STAMP];
	if(imOnRoad){
	    //jestem na trakcie - należy odesłać sprzeciw
            return 1;  
	}
        if(myTimeStamp < timeStamp){
            //Znacznik czasowy tego procesu jest mniejszy, nalezy odesłać sprzeciw
            return 1;  
        } else if(myTimeStamp == timeStamp){
            //Równy piorytet - należy sprawdzić id procesu, aby wybrać lepszy
            int senderId = message[SENDER_ID];
            if(myId < senderId){
                //Id tego procesu jest lepsze, nalezy odesłac sprzeciw
                return 1;
            } else {
                //Proces ma lepsze id, nigdy nie będą równe
                return 0;
            }        
        } else {
            //Odebrany znacznik czasowy jest mniejszy niż mój
            return 0;
        }        
    } else {
        //Proces ubiega się o inny trakt
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
            recivedAllMessages = true;
            pthread_cond_signal(&cond);
        }        
        pthread_mutex_unlock(&mutex); 
    }     
    return NULL;
}
       

void localSection(int maxWaitTime){
    printf(KGRN "%d: oczekuje na rozkazy w sekcji lokalnej\n", myId);

    int waitTime = rand() % (maxWaitTime + 1);
    printf(KNRM "%d: poczekam %d sekund\n", myId, waitTime);
    sleep(waitTime);
}

void criticalSection(int maxWaitTime){
    imOnRoad = true;
    printf(KGRN "%d: Przechodzę przez trakt %d\n", myId, myRoadNumber);

    int waitTime = rand() % (maxWaitTime + 1);
    printf(KNRM "%d: będę przechodzić przez %d sekund\n", myId, waitTime);
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

    


    MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &myId);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    srand(time(NULL) + myId);

    int roadsSize[] = {
        100,
	120

    };

    int legionsSize[] = {
        90,
        50,
	40,
	10,
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
    int maxWaitTime = 10;


    pthread_t t1;
    if(pthread_create(&t1, NULL, messageReciver, &message)){
        perror("Blad: Tworzenie watku! \n");
    }

    //Petla główna
    while(numberOfPasses < maxNumberOfPasses){
        localSection(maxWaitTime);

        //Losowy wybór traktu
        myRoadNumber = rand() % NUMBER_OF_ROADS;

        recivedMessages = 0;
        recivedAllMessages = false;
        isMoreSpace = false;
        askForSpace(myRoadNumber);

        pthread_mutex_lock(&mutex);
        while(!recivedAllMessages){
            //Oczekiwanie na odpowiedź od wszystkich procesów
            pthread_cond_wait(&cond, &mutex);
        }	
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
            pthread_mutex_lock(&mutexSpace);
            while(!isMoreSpace){
                //Oczekiwanie na zwolnienie miejsca
                pthread_cond_wait(&condSpace, &mutexSpace);
            }
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
        criticalSection(maxWaitTime);
        //Broadcast o zwolnieniu miejsca
        freeSpace();
        printf(KYEL "%d: zwalniam miejsce na trakcie\n", myId);

    }


    printf(KNRM "%d: Koncze dzialanie\n", myId);
    
		
	MPI_Finalize();
}
