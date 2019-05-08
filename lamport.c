#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MSG_SIZE 4
//0 - Numer traktu
//1 - Znacznik czasowy
//2 - Id procesu
//3 - Typ informacji

//Typy informacji
#define ACCPET 10
#define REFUSE 11
#define FREE 12
#define ASK_FOR_SPACE 14

#define MSG_TAG_TOKEN 10
#define TOKEN_MAX_VALUE 10
#define NUMBER_OF_LEGIONS 10
#define NUMBER_OF_ROADS 10

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

/**
 * Zwraca 1 jeżeli należy odesłać potwiedzenie
 * Zwraca 0 jeżeli nalezy odesłac sprzeciw
**/
int askForSpace(int* message){
    int roadNumber = message[ROAD_NUMBER];
    if(myRoadNumber == roadNumber){
        //Proces ubiega się o ten sam trakt 
        int timeStamp = message[TIME_STAMP];
        if(myTimeStamp <= timeStamp){
            if(myTimeStamp == timeStamp){
                //Równy piorytet - należy sprawdzić id procesu, aby wybrać lepszy
                int senderId = message[SENDER_ID];
                if(myId < senderId){
                    //Id tego procesu jest lepsze, nalezy odesłac sprzeciw
                    return 0;
                } else {
                    //Proces ma lepsze id, nigdy nie będą równe
                    return 1;
                }
            } else {
                //Znacznik czasowy tego procesu jest lepszy, nalezy odesłać sprzeciw
                return 0;
            }   
        } else {
            //Proces ma lepszy piorytet
            return 1;
        }
    } else {
        //Proces ubiega się o inny trakt
        return 1;
    }
}


void acceptMessage(nt* message){
    int reciver = message[SENDER_ID];

    int* newMessage;
    newMessage[ROAD_NUMBER] = myRoadNumber;
    newMessage[TIME_STAMP] = myTimeStamp;
    newMessage[SENDER_ID] = myId;
    newMessage[MESSAGE_TYPE] = ACCPET;

    MPI_Send(newMessage, MSG_SIZE, MPI_INT, reciver, MSG_TAG_TOKEN, MPI_COMM_WORLD);    
}

void refuseMessage(int* message){

    int reciver = message[SENDER_ID];

    int* newMessage;
    newMessage[ROAD_NUMBER] = myRoadNumber;
    newMessage[TIME_STAMP] = myTimeStamp;
    newMessage[SENDER_ID] = myId;
    newMessage[MESSAGE_TYPE] = REFUSE;

    MPI_Send(newMessage, MSG_SIZE, MPI_INT, reciver, MSG_TAG_TOKEN, MPI_COMM_WORLD);
}



void *messageReciver(void *message_c){
    int *message = (int*)message_c;
    while(1){
        MPI_Recv(message, MSG_SIZE, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        switch(message[MESSAGE_TYPE]){
            case ACCPET:
                //Proces wysyłający wiadomość zezwala na wejście na trakt - jest dalej w kolejce lub nie ubiega się o ten trakt
                int senderId = message[SENDER_ID];
                legions[senderId] = 1;
            break;
            case REFUSE:
                //Proces wysyłający wiadomość odmawia zajęcia traktu - jest szybciej w kolejce do niego
                int senderId = message[SENDER_ID];
                legions[senderId] = 0;
            break;
            case FREE:
                //Proces wysyłający wiadomość zwolnił miejsce na trakcie
                //Należy usunąć go z listy procesów zajmujących / oczekujących na trakt, jeżeli ten proces czeka na ten trakt
                int senderId = message[SENDER_ID];
                int senderRoadNumber = message[ROAD_NUMBER];
                if(senderRoadNumber == myRoadNumber){
                    legions[senderId] = 1;
                }                
            break;
            case ASK_FOR_SPACE:
                if(askForSpace(message) == 1){
                    //Odesłanie potwiedzenia
                    acceptMessage();
                } else {
                    //odesłanie sprzeciwu
                    refuseMessage(message);
                }
            break;
            default:
        }         
    }     
    return NULL;
}


void localSection(int maxWaitTime){
    printf("%d: oczekuje na rozkazy w sekcji lokalnej\n", myId);

    int waitTime = rand() % (maxWaitTime + 1);
    sleep(waitTime);
}


int main(int argc, char **argv)token
{

    srand(time(NULL));

	int size;
    MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &myId);
    MPI_Comm_size(MPI_COMM_WORLD, &size);


    //Ustalenie warości poczatkowych rozmiarow traktów


    //Trzeba utworzyć drugi wątek który odbiera wiadomości, żeby inne procesy nie blokowały się na sendzie kiedy ten jest w sekcji krytycznej
    pthread_t t1;
    if(pthread_create(&t1, NULL, messageReciver, &message)){
        perror("Blad: Tworzenie watku! \n");
    }

    //Liczba marszy po traktach
    int numberOfPasses = 0;

    //Maksymalna liczba marszy
    int maxNumberOfPasses = 100;
    
    //Maksymalny czas oczekiwania na rozkazy (w sekundach)
    int maxWaitTime = 10;

    //Petla główna
    while(numberOfPasses < maxNumberOfPasses){
        localSection(maxWaitTime);

        //Losowy wybór traktu
        myRoadNumber = rand % NUMBER_OF_ROADS;

        askForSpace(myRoadNumber);
    }








    printf("%d: Koncze dzialanie\n", myId);
    
		
	MPI_Finalize();
}
