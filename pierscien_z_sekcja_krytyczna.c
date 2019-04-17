#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MSG_SIZE 4
//0 - id procesu nadawcy
//1 - miejsce w kolejce
//2 - który trakt
//3 - typ informacji

//Typy informacji
#define ACCPET 10
#define REFUSE 11
#define FREE 12
#define TAKE 13
#define ASK_FOR_SPACE 14

#define MSG_TAG_TOKEN 10
#define TOKEN_MAX_VALUE 10
#define NUMBER_OF_LEGIONS 10
#define NUMBER_OF_ROADS 10

#define SENDER_ID 0
#define QUEUE_PLACE 1
#define ROAD_NUMBER 2
#define MESSAGE_TYPE 3

//Zmienna globalna - współdzielona
int tokenRecived;
MPI_Status status;
int message[MSG_SIZE];
int legions[NUMBER_OF_LEGIONS];
int roads[NUMBER_OF_ROADS];

void *odbieranieWiadomosci(void *message_c){
    int *message = (int*)message_c;
    while(1){
        MPI_Recv(message, MSG_SIZE, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        switch(message[MESSAGE_TYPE]){
            case ACCPET:
                //Proces wysyłający wiadomość zezwala na wejście na trakt - jest dalej w kolejce lub nie ubiega się o ten trakt
            break;
            case REFUSE:
                //Proces wysyłający wiadomość odmawia zajęcia traktu - jest szybciej w kolejce do niego
            break;
            case FREE:
                //Proces wysyłający wiadomość zwolnił miejsce na trakcie
                roads[message[SENDER_ID]] += legions[message[SENDER_ID]];
            break;
            case TAKE:
                //Proces wysyłający wiadomość zajął miejsce na trakcie
                roads[message[SENDER_ID]] -= legions[message[SENDER_ID]];
            break;
            case ASK_FOR_SPACE:
                //Proces wysyłający wiadomość chce zająć trakt
            break;
            default:
        }         
    }     
    return NULL;
}

int main(int argc, char **argv)
{

    srand(time(NULL));
    
	int rank, size, reciver;

    int first = 0;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);


    if(rank < size - 1){
        reciver = rank + 1;
    } else {
        reciver = 0;
    }


    //inicjowanie pierścienia
    token[0] = 0;
    if(rank == first){
        printf("%d: Wysylam wartosc %d do %d\n", rank, token[0], reciver);
        MPI_Send(token, MSG_SIZE, MPI_INT, reciver, MSG_TAG_TOKEN, MPI_COMM_WORLD);
    }  
    
    
    tokenRecived = 0;
    //Trzeba utworzyć drugi wątek który odbiera wiadomości, żeby inne procesy nie blokowały się na sendzie kiedy ten jest w sekcji krytycznej
    pthread_t t1;
    if(pthread_create(&t1, NULL, odbieranieWiadomosci, &token)){
        perror("Blad: Tworzenie watku! \n");
    }

    
    //jeżeli wartość tokena + liczba procesów > max wartość tokena, to token już do mnie nie trafi
    while(token[0] + size <= TOKEN_MAX_VALUE){

        //Nie przyszedł token - wchodzę do sekcji lokalnej
        while(tokenRecived == 0){
            //sekcja lokalna - nie może zostac przerwana - po jej zakończeniu przesyłam dalej 
            //token - jeżeli przyszedł, jeżeli nie - wchodzę znowu do sekcji lokalnej
            for(int i = 0; i < 5; i++){
                printf("%d: jestem w sekcji lokalnej po raz %d\n", rank, i);
                sleep(1);
            }
        }     
        tokenRecived = 0;
        if(token[0] <= TOKEN_MAX_VALUE){
            printf("%d: Wysylam wartosc %d do %d\n", rank, token[0], reciver);
            MPI_Send(token, MSG_SIZE, MPI_INT, reciver, MSG_TAG_TOKEN, MPI_COMM_WORLD);     
        }
        token[0] -= 1;
    }

    printf("%d: Koncze dzialanie\n", rank);
    
		
	MPI_Finalize();
}
