#include<sys/socket.h>
#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<netinet/ip.h>
#include<netinet/tcp.h>
#include<string.h>
#include<errno.h>
#include<pthread.h>
#include<arpa/inet.h>
#include<signal.h>
#include<time.h>
#include "List.h"

#define MYPORT 23456
#define BUF_SIZE 1024

/* DEFINED INSIDE List.h
// Client informations
typedef struct client_inf {
    char IP_address[32]; // Holds the client IP address
    char nickname[32]; // Holds the nickname chosen by the the user
    int client_sd ; // The socket_descriptor opened between client and server
    struct client_inf* last_chat ; // Pointer to the last user we chatted with. Usefull for avoiding two random chats in a row with the same user
} thread_arg ;*/

// GENERAL FUNCTIONS
// Initializes socket, binds the special address INADDR_ANY to the socket, and put the socket in a listen state with backlog = qlen. Returns a socket descriptor or -1 in case there will be any error
int initServerSocket(int type, const struct sockaddr *addr, socklen_t alen, int qlen);
// Creates and allocate all necessaries threads and data structures to allow the server operations
void initServerMatchingEngine();
// Handler of signals
void signalHandler (int numSignal);
// Returns -1 for unknown or extraneous requests, a positive number which will indicates the type of request otherwise
int parse_client_request(const char* request_buffer);
// Entrypoint of the thread that will manage one client at time
void *manage_a_single_client(void *arg);
// Entrypoint of the thread that will pair clients in a chatroom waitlist who looks for a conversation
void *pair_clients(void *arg);
// Entrypoint of the thread that will manage a conversations between two clients. Launched by pair_clients
void *manage_a_conversation(void *arg);

//GLOBAL LISTS OF CONNECTED USERS WAITING TO CHAT
linkedList* climate_change_room;
linkedList* travel_related_room;
linkedList* horror_movies_room;

// Counters of active conversations
int totalNumberOfActiveChats, totalNumberOfUsers ;


pthread_mutex_t n_total_active_chats_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t n_total_users_mutex = PTHREAD_MUTEX_INITIALIZER;


// Main Entrypoint
int main(int argc, char* argv[]){

  int server_socket ;
  struct sockaddr_in server_address ;

  // Ignoring the SIGPIPE generated when writing on a socket which connection has crashed
  if(signal(SIGPIPE,signalHandler) == SIG_ERR ){
      perror("Signal error ");
      return (-2) ;
  }

  // Ignoring the SIGPIPE generated when writing on a socket which connection has crashed
  if(signal(SIGINT,signalHandler) == SIG_ERR ){
      perror("Signal error ");
      return (-3) ;
  }

  // Preparing the server address
  memset(&server_address, '0', sizeof(server_address));
  server_address.sin_family = AF_INET ;
  server_address.sin_port = htons(MYPORT);
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);

  while( (server_socket = initServerSocket(SOCK_STREAM,(struct sockaddr*)&server_address,sizeof(server_address),10)) < 0 ){
    printf("Error during init. of the server ... \n");
    printf("Wait for another try or press Ctrl-C to terminate ... \n");
    sleep(3);
  }

  initServerMatchingEngine();

  // Parameters for the accept
  int client_socket;
  struct sockaddr_in client_address ;
  socklen_t client_addr_size = sizeof(client_address);

  // Needed for printing the IP address of a client in a human readable format
  const char* address_dot_format ;
  char buffer_address_dot_format[INET_ADDRSTRLEN];

  // Parameters for launching threads
  pthread_t tinfo;
  int err;
  pthread_attr_t  t_attr;
  thread_arg* client_info;

  // Server main cycle
  while(1){

    client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_addr_size);

    if(client_socket>0){

        // LOGGING NEW CONNECTIONS
        address_dot_format = inet_ntop(AF_INET, &client_address.sin_addr, buffer_address_dot_format, INET_ADDRSTRLEN);
        printf("\n-NEW CLIENT CONNECTED :\nSocket Descriptor : %d\nIP ADDRESS : %s\n",client_socket,address_dot_format);

        /* Mutex needed because increasing the totalNumberOfUsers variable can lead to a race condition with another thread
         * that wants to do the opposite for a user who is logging out.
         */
        pthread_mutex_lock(&n_total_users_mutex);
        totalNumberOfUsers++;
        pthread_mutex_unlock(&n_total_users_mutex);

        // Preparing the struct to pass for every new "manage_a_single_client" thread
        client_info = (thread_arg *)malloc(sizeof(thread_arg));
        client_info->client_sd = client_socket ;
        strcpy(client_info->IP_address, address_dot_format) ;
        memset(client_info->nickname, '\0', sizeof(client_info->nickname));
        client_info->last_chat = NULL ;



        err = pthread_attr_init(&t_attr);
        if (err != 0)
            printf("Error pthread_attr_init : %s\n", strerror(err));
        err = pthread_attr_setdetachstate(&t_attr, PTHREAD_CREATE_DETACHED);

        // Threads creation. They need to be detached so performance won't deteriorate during time cause of zombies
        if(err == 0){
          if ( (err=pthread_create(&tinfo, &t_attr, manage_a_single_client, (void*)client_info) ) ) {
            char threadErrorMessage[128] = "Error from the server accepting connection, please restart the client !\n\0";
            printf("Error calling pthread_create : %s\n", strerror(err));
            write(client_socket,threadErrorMessage,128);
            free(client_info);
          }
        }
        pthread_attr_destroy(&t_attr);
    }
  }

  return 0 ;
}

// GENERAL FUNCTIONS

// Initializes socket, binds the special address INADDR_ANY to the socket, and put the socket in a listen state with backlog = qlen
// Returns a socket descriptor or -1 in the case there will be any error
int initServerSocket(int type, const struct sockaddr *addr, socklen_t alen, int qlen) {
           int fd ;
           int reuse = 1;
           if ((fd = socket(addr->sa_family, type, 0)) < 0){
               printf("Error calling socket() \n");
               return(-1);
           }
           // It allows to reuse the same ip_address in case we restart the Server after some seconds of shutting down
           if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int)) < 0){
               printf("Error calling setsockopt() \n");
               goto errout;
           }

           int flags = 10;
           if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void *)&flags, sizeof(flags))){
               printf("ERROR: setsocketopt(), SO_KEEPIDLE");
               goto errout;

           }

           flags = 5;
           if (setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void *)&flags, sizeof(flags))){
               printf("ERROR: setsocketopt(), SO_KEEPCNT");
               goto errout;

           }

           flags = 5;
           if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *)&flags, sizeof(flags))){
               printf("ERROR: setsocketopt(), SO_KEEPINTVL");
               goto errout;
           }


           if (bind(fd, addr, alen) < 0){
               printf("Error calling bind() \n");
               goto errout;
           }

           if (type == SOCK_STREAM || type == SOCK_SEQPACKET){
              if (listen(fd, qlen) < 0){
                   printf("Error calling listen() \n");
                   goto errout;
              }
           }
           return(fd);
    errout:
    close(fd);
    return(-1);
}

// Creates and allocate all necessaries threads and data structures to allow the server operations
void initServerMatchingEngine(){

  // Initialization of rooms' waiting lists
  climate_change_room = createANewLinkedList();
  travel_related_room = createANewLinkedList();
  horror_movies_room = createANewLinkedList();
  // If there is any error allocating the lists the server will crash and needs to be restarted
  if (climate_change_room == NULL || travel_related_room == NULL || horror_movies_room == NULL)
    kill(getpid(),SIGINT);

  // Initialization of counters about active chats between users and connected users
  totalNumberOfActiveChats = 0;
  totalNumberOfUsers = 0 ;

  // Parameters for launching threads
  pthread_t tinfo;
  int err;

  // If there is any error launching the pair_clients threads the server will crash and needs to be restarted
  if ( (err=pthread_create(&tinfo, NULL, pair_clients, (void*)climate_change_room) ) ) {
      printf("Error calling pthread_create pair_clients climate : %s\nRestart the server.\n", strerror(err));
      kill(getpid(),SIGINT);
  }

  if ( (err=pthread_create(&tinfo, NULL, pair_clients, (void*)travel_related_room) ) ) {
      printf("Error calling pthread_create pair_clients travel : %s\nRestart the server.\n", strerror(err));
      kill(getpid(),SIGINT);
  }

  if ( (err=pthread_create(&tinfo, NULL, pair_clients, (void*)horror_movies_room) ) ) {
      printf("Error calling pthread_create pair_clients horror : %s\nRestart the server.\n", strerror(err));
      kill(getpid(),SIGINT);
  }

}

// Handler of signals SIGPIPE when writing on a closed socket, and SIGINT for closing the server
void signalHandler (int numSignal){
  if (numSignal == SIGINT){
    printf("\nClosing the server ...\n\n");
    if (climate_change_room == NULL || travel_related_room == NULL || horror_movies_room == NULL)
      printf("\nThere has been an error allocating data structures, try to restart the server.\n\n");
    else{
      destroy_list(climate_change_room);
      destroy_list(travel_related_room);
      destroy_list(horror_movies_room);
    }
    exit(0);
  }
  if (numSignal == SIGPIPE){
    printf("Error trying to send response to the client...\n\n");
  }
}

// Returns -1 for unknown or extraneous requests, a positive number which will indicates the type of request otherwise
int parse_client_request(const char* request_buffer){

  size_t less_index, major_index ;
  char *find_less; // Less is intended to be the char '<'
  char *find_major; // Major instead '>'

  find_less = strchr(request_buffer,'<');
  find_major = strchr(request_buffer,'>');
  if ( find_less != NULL && find_major != NULL ){

    less_index = (size_t)(find_less - request_buffer);
    major_index = (size_t)(find_major - request_buffer);

    // If after the '>' there is somethig else, then wrong syntax
    if (request_buffer[major_index+1]!='\n' && request_buffer[major_index+1]!='\0')
      goto invalidSyntax ;

    // If the request doesn't start with //command: or //command:START , then wrong syntax
    if (strncmp(request_buffer,"//command:",less_index)!=0){ //&& strncmp(request_buffer,"//command:START",less_index)!=0)
      if(strncmp(request_buffer,"//command:START",less_index)!=0){
        if(strncmp(request_buffer,"//command:NICKNAME",less_index)!=0){
          goto invalidSyntax;
        }else{
          return 9;
        }
      }else{
        if (strncmp(find_less,"<Climate change>", major_index-less_index+1 )==0 ){
          return 2;
        }else if (strncmp(find_less,"<Travel related>", major_index-less_index+1 )==0 ){
          return 3;
        }else if (strncmp(find_less,"<Horror movies>", major_index-less_index+1 )==0 ){
          return 4;
        }else{
          return -2;
        }

      }
    }else{
      if (strncmp(find_less,"<USERS>", major_index-less_index+1 )==0 ){
        return 1;
      }else if( strncmp(find_less,"<REROLL>", major_index-less_index+1 )==0 ){
        return 5;
      }else if (strncmp( find_less,"<STOP>", major_index-less_index+1 )==0 ){
        return 6;
      }else if (strncmp( find_less,"<ROOMS>", major_index-less_index+1 )==0 ){
        return 7;
      }else if (strncmp( find_less,"<HELP>", major_index-less_index+1 )==0 ){
        return 8;
      }else{
        return 0;
      }
    }

  }else{
    goto invalidSyntax;
  }

  invalidSyntax:
  return -1 ;
}

// Entrypoint of the thread that will take care of established connections
void *manage_a_single_client(void *arg) {

  thread_arg* client_info = (thread_arg*)arg;
	char recv_buff[BUF_SIZE];
  char send_buff[BUF_SIZE];
	int n_read_char, dim_recv_messagge = 0;

  // Main cicle serving the client
	while (1){

    int flags = 1;
    if (setsockopt(client_info->client_sd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags))){
      goto gone_client;
    }

    n_read_char = read(client_info->client_sd, recv_buff+dim_recv_messagge, BUF_SIZE-dim_recv_messagge-1);

    if(n_read_char < 0){
  		printf("\n Error receiveing message from the client \n");
  	}else if(n_read_char > 0){
      dim_recv_messagge += n_read_char;
      // If the space in the buffer has finished, or newline appears in the string then we can try to process the request
      if(dim_recv_messagge >= BUF_SIZE-1 || recv_buff[dim_recv_messagge-1] == '\n'){

        if(dim_recv_messagge < BUF_SIZE-1)
          recv_buff[dim_recv_messagge]='\0';
        else // buffer is full and we truncate the read string
          recv_buff[BUF_SIZE-1]='\0';

        // LOGGING A NEW REQUEST
        printf("\n-NEW REQUEST FROM CLIENT :\nNickname : %s\nSocket Descriptor : %d\nIP ADDRESS : %s\nRequest : %s",client_info->nickname,client_info->client_sd,client_info->IP_address,recv_buff);

        int request_type = parse_client_request(recv_buff);
        if (request_type<0){ // Invalid syntax
          if (request_type==-1){
            sprintf(send_buff, "\nThe request can't be executed by the server because of the wrong syntax !\nExpected : //command:<...> OR //command:START<room name>\n");
            printf("The request can't be executed by the server because of the wrong syntax !\n");
          }
          if (request_type==-2){
            sprintf(send_buff, "\nThe request can't be executed by the server because there's no room with such name\n");
            printf("The request can't be executed by the server because there's no room with such name\n");
          }
          write(client_info->client_sd,send_buff,strlen(send_buff));
        } else if ( request_type == 0 ){ // Syntax is right but the command has not been found
          sprintf(send_buff, "\nThe request can't be executed by the server ! No command found !\n");
          write(client_info->client_sd,send_buff,strlen(send_buff));
          printf("The request can't be executed by the server ! No command found !\n");
        } else if (request_type == 1){ // request : //command:<numberOfUsers>
          int totalClimate = sizeOfTheList(climate_change_room);

          int totalTravel = sizeOfTheList(travel_related_room);

          int totalHorror = sizeOfTheList(horror_movies_room);

          pthread_mutex_lock(&n_total_active_chats_mutex);
          pthread_mutex_lock(&n_total_users_mutex);
          sprintf(send_buff, "\n*** NUMBER OF USERS ***\n- Waiting in the \"Climate change\" room : %d \n- Waiting in the \"Travel related\" room : %d \n- Waiting in the \"Horror movies\" room : %d \n*** TOTAL NUMBER OF ACTIVE CHATS BETWEEN USERS : %d ***\n*** TOTAL NUMBER OF USERS CONNECTED : %d ***\n", totalClimate,totalTravel,totalHorror,totalNumberOfActiveChats,totalNumberOfUsers);
          pthread_mutex_unlock(&n_total_active_chats_mutex);
          pthread_mutex_unlock(&n_total_users_mutex);
          write(client_info->client_sd,send_buff,strlen(send_buff));
        } else if (request_type == 2){
          // if command:START<Climate change> add user info into the list of choice
          linkedListNode* new_node ;
          if ( (new_node=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
            new_node->data = client_info ;
            new_node->next = NULL ;
          }
          insert_element(new_node,climate_change_room);
          sprintf(send_buff, "\nLooking for someone to chat with in the \"Climate change\" room ...\nCtrl+C to exit ...\n");
          write(client_info->client_sd,send_buff,strlen(send_buff));
          return 0;
          // exit this thread
        } else if (request_type == 3){
          // if command:START<Travel related> add user info into the list of choice
          linkedListNode* new_node ;
          if ( (new_node=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
            new_node->data = client_info ;
            new_node->next = NULL ;
          }
          insert_element(new_node,travel_related_room);
          sprintf(send_buff, "\nLooking for someone to chat with in the \"Travel related\" room ...\nCtrl+C to exit ...\n");
          write(client_info->client_sd,send_buff,strlen(send_buff));
          return 0;
          // exit this thread
        } else if (request_type == 4){
          // if command:START<Horror movies> add user info into the list of choice
          linkedListNode* new_node ;
          if ( (new_node=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
            new_node->data = client_info ;
            new_node->next = NULL ;
          }
          insert_element(new_node,horror_movies_room);
          sprintf(send_buff, "\nLooking for someone to chat with in the \"Horror movies\" room ...\nCtrl+C to exit ...\n");
          write(client_info->client_sd,send_buff,strlen(send_buff));
          return 0;
          // exit this thread
        } else if (request_type == 7){
          sprintf(send_buff, "\n*** AVAILABLE ROOMS ***\n-\"Climate change\" room : Greta would be proud of you \n-\"Travel related\" room : Do you enjoy going around the world ?  \n-\"Horror movies\" room : Creepy topics around here \n\n");
          write(client_info->client_sd,send_buff,strlen(send_buff));
        } else if (request_type == 8){
          sprintf(send_buff, "--- LISTA DEI COMANDI DISPONIBILI ---\n* Visualizza numero di utenti per ogni stanza a tema             : //command:<USERS> \n* Visualizza quante e quali sono le stanze a tema disponibili    : //command:<ROOMS> \n* Avvia una chat casuale con un altro host all'interno di <room> : //command:START<room name> \n* Terminare immediatamente il programma in esecuzione            : Ctrl+D or Ctrl-C \n\n");
          write(client_info->client_sd,send_buff,strlen(send_buff));
        } else if (request_type == 9){

          recv_buff[strlen(recv_buff)-1]='\0';// Removes '\n'
          recv_buff[strlen(recv_buff)-1]='\0';// and '>' from the nickname
          strcpy(client_info->nickname,recv_buff+19);

          sprintf(send_buff, "Nickname impostato correttamente come : <%s>\n",client_info->nickname);
          write(client_info->client_sd,send_buff,strlen(send_buff));

        }
        dim_recv_messagge = 0;
      }

    }else if(n_read_char == 0){
      // If read returns 0 the socket with the client and the connection has been closed
      goto gone_client;
    }


  }

   gone_client:
   // LOGGING DISCONNECTIONS
   printf("\n-A CLIENT DISCONNECTED :\nNickname : %s\nSocket Descriptor : %d\nIP ADDRESS : %s\n",client_info->nickname,client_info->client_sd,client_info->IP_address);
   close(client_info->client_sd);
   free(client_info);
   pthread_mutex_lock(&n_total_users_mutex);
   totalNumberOfUsers--;
   pthread_mutex_unlock(&n_total_users_mutex);
   return 0; // Implicit call to pthread_exit

}

// Entrypoint of the thread that will pair clients which look out for a conversation
void *pair_clients(void *arg){

  linkedList* waitlist = (linkedList*)arg;

  while (1) {
    int listSize = sizeOfTheList(waitlist);
    if (listSize>1) {
      // Tirare fuori due indici random
      srand(time(NULL));
      int firstUser = rand()%listSize;
      int secondUser = rand()%listSize;
      // Controllare che gli indici siano diversi, in caso contrario, sommare ad uno dei due 1 e fare modulo listSize
      if (firstUser == secondUser){
        secondUser = (secondUser+1)%listSize ;
      }
      // accediamo via accessByIndex
      linkedListNode* firstUserNode = accessByIndex(firstUser,waitlist);
      linkedListNode* secondUserNode = accessByIndex(secondUser,waitlist);

      thread_arg* firstUserInfo = NULL ;
      thread_arg* secondUserInfo = NULL ;
      if (firstUserNode!=NULL && secondUserNode!=NULL){
        firstUserInfo = firstUserNode->data;
        secondUserInfo = secondUserNode->data;
      }

      if(firstUserInfo!=NULL && secondUserInfo!=NULL){

        // Invariante : gli utenti sono diversi, pertanto se uno dei due non è mai stato accoppiato con nessun altro (last_chat==NULL) oppure entrambi hanno parlato con due persone diverse procediamo
        if (firstUserInfo->last_chat==NULL || secondUserInfo->last_chat==NULL || (firstUserInfo->last_chat!=(struct client_inf*)secondUserInfo && secondUserInfo->last_chat!=(struct client_inf*)firstUserInfo)){
          // aggiorna le due last chat
          firstUserInfo->last_chat=(struct client_inf*)secondUserInfo;
          secondUserInfo->last_chat=(struct client_inf*)firstUserInfo;
          // rimuovere i due utenti dalla waitlist
          remove_element(firstUserNode, waitlist);
          remove_element(secondUserNode, waitlist);
          // Tiene conto della nuova conversazione avviata
          pthread_mutex_lock(&n_total_active_chats_mutex);
          totalNumberOfActiveChats++;
          pthread_mutex_unlock(&n_total_active_chats_mutex);

          // lancia conversazione
          conversation_thread_arg* conversation_info = (conversation_thread_arg*)malloc(sizeof(conversation_thread_arg));
          conversation_info->firstUserInfo = firstUserInfo;
          conversation_info->secondUserInfo = secondUserInfo;
          conversation_info->waitlist = waitlist;
          pthread_t tinfo;
          int err;

          // If there is any error launching the manage_a_conversation thread the server will put both users in the waitlist
          if ( (err=pthread_create(&tinfo, NULL, manage_a_conversation, (void*)conversation_info) ) ) {
              printf("Error calling pthread_create manage_a_conversation : %s\n", strerror(err));

              firstUserInfo->last_chat=NULL;
              secondUserInfo->last_chat=NULL;

              linkedListNode* new_node ;
              if ( (new_node=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
                new_node->data = firstUserInfo ;
                new_node->next = NULL ;
              }
              insert_element(new_node,waitlist);
              if ( (new_node=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
                new_node->data = secondUserInfo ;
                new_node->next = NULL ;
              }
              insert_element(new_node,waitlist);

              pthread_mutex_lock(&n_total_active_chats_mutex);
              totalNumberOfActiveChats--;
              pthread_mutex_unlock(&n_total_active_chats_mutex);

              conversation_info->firstUserInfo = NULL;
              conversation_info->secondUserInfo = NULL;
              conversation_info->waitlist = NULL;
              free (conversation_info);
           }
           if(!err){
            pthread_detach(tinfo);
          }
        }
      }
    }
  }
}

// Entrypoint of the thread that will manage a conversations between two clients. Launched by pair_clients
void *manage_a_conversation(void *arg){

  conversation_thread_arg* conversation_info = (conversation_thread_arg*)arg ;

  int firstUserSD = conversation_info->firstUserInfo->client_sd;
  int secondUserSD = conversation_info->secondUserInfo->client_sd ;
  char recv_buff[BUF_SIZE-64];
  char send_buff[BUF_SIZE];
  int n_read_char;

  sprintf(send_buff, "\nA NEW MATCH HAS BEEN FOUND !\n\nPress //command:<STOP> or //command:<REROLL> or Ctrl+C to exit\n\nSAY HI TO : %s\n\n",conversation_info->secondUserInfo->nickname);
  write(firstUserSD,send_buff,strlen(send_buff));
  sprintf(send_buff, "\nA NEW MATCH HAS BEEN FOUND !\n\nPress //command:<STOP> or //command:<REROLL> or Ctrl+C to exit\n\nSAY HI TO : %s\n\n",conversation_info->firstUserInfo->nickname);
  write(secondUserSD,send_buff,strlen(send_buff));


  int maxD;
  fd_set read_fds;
  int num_descriptors;

  if(firstUserSD>=secondUserSD)
    maxD = firstUserSD+1;
  else
    maxD = secondUserSD+1;

  while (1) {

    int flags = 1;
    setsockopt(firstUserSD, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
    flags = 1;
    setsockopt(secondUserSD, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));

    FD_ZERO(&read_fds);
    FD_SET(firstUserSD,&read_fds);
    FD_SET(secondUserSD,&read_fds);

    num_descriptors = select(maxD,&read_fds,NULL,NULL,NULL);
    if (num_descriptors<0){
      printf("Error calling select : %s\nConversation has ended\n", strerror(num_descriptors));
      // Mettere entrambi in attesa di una nuova chat
      goto reroll ;
    }else{

      if (FD_ISSET(firstUserSD, &read_fds)){
        if ( (n_read_char = read(firstUserSD, recv_buff, BUF_SIZE-64)) != 0){
          if(n_read_char < 0){
            sprintf(send_buff, "\nError sending the message. Try again !\n");
            write(firstUserSD,send_buff,strlen(send_buff));
        	}else{
            recv_buff[n_read_char]='\0';
            // Se non si tratta del comando di exit o di next_chat inoltra al secondo utente
            int result_parsing_request = parse_client_request(recv_buff);
            if ( result_parsing_request!=5 && result_parsing_request!=6 ){
              sprintf(send_buff, "\n-- <%s> --\n%s",conversation_info->firstUserInfo->nickname,recv_buff);
              write(secondUserSD,send_buff,strlen(send_buff));
            }else{
              if ( result_parsing_request==5 ){
                goto reroll ;
              }
              else {
                // DA FARE
                goto user1_stopped;
              }
            }
          }
        }else{
          // Reading 0 means the client "firstUserSD" has disconnected
          goto user1_disconnected;
        }
      }

      if (FD_ISSET(secondUserSD, &read_fds)){
        if ( (n_read_char = read(secondUserSD, recv_buff, BUF_SIZE-64)) != 0){
          if(n_read_char < 0){
            sprintf(send_buff, "\nError sending the message. Try again !\n");
            write(secondUserSD,send_buff,strlen(send_buff));
        	}else{
            recv_buff[n_read_char]='\0';
            // Se non si tratta del comando di exit o di next_chat inoltra al primo utente
            int result_parsing_request = parse_client_request(recv_buff);
            if ( result_parsing_request!=5 && result_parsing_request!=6 ){
              sprintf(send_buff, "\n-- <%s> --\n%s",conversation_info->secondUserInfo->nickname,recv_buff);
              write(firstUserSD,send_buff,strlen(send_buff));
            }else{
              if ( result_parsing_request==5 ){
                goto reroll ;
              }
              else{
                // DA FARE
                goto user2_stopped ;
              }
            }
          }
        }else{
          // Reading 0 means the client "secondUserSD" has disconnected
          goto user2_disconnected;

        }
      }


    }
  }

  user1_stopped:
  // Comunica al secondo utente che è finita la conversazione
  sprintf(send_buff, "\n%s has closed the conversation\nLooking for someone else ...\nCtrl+C to exit ...\n",conversation_info->firstUserInfo->nickname);
  write(secondUserSD,send_buff,strlen(send_buff));
  sprintf(send_buff, "\nYou have closed the conversation and stopped rolling...\n\n***** BENVENUTI IN RANDOMCHAT ! *****\n\n--- Digitare //command:<HELP> per conoscere i comandi disponibili ---\n\n");
  write(firstUserSD,send_buff,strlen(send_buff));

  pthread_mutex_lock(&n_total_active_chats_mutex);
  totalNumberOfActiveChats--;
  pthread_mutex_unlock(&n_total_active_chats_mutex);

  // Fa ritornare il secondo utente in attesa di chattare
  linkedListNode* new_node_user1_stop ;
  if ( (new_node_user1_stop=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
    new_node_user1_stop->data = conversation_info->secondUserInfo ;
    new_node_user1_stop->next = NULL ;
  }
  insert_element(new_node_user1_stop,conversation_info->waitlist);

  // Affida la gestione del primo utente al thread "manage_a_single_client", e nel caso ci sia un errore, effettua la disconnessione.
  pthread_t t1_info;
  int err1 ;
  if ( (err1=pthread_create(&t1_info, NULL, manage_a_single_client, (void*)conversation_info->firstUserInfo) ) ) {
    char threadErrorMessage[128] = "Error from the server, please restart the client !\n\0";
    write(firstUserSD,threadErrorMessage,128);

    // LOGGING DISCONNECTION
    printf("\n-A CLIENT DISCONNECTED :\nNickname : %s\nSocket Descriptor : %d\nIP ADDRESS : %s\n",conversation_info->firstUserInfo->nickname,firstUserSD,conversation_info->firstUserInfo->IP_address);
    close(firstUserSD);
    free(conversation_info->firstUserInfo);
    pthread_mutex_lock(&n_total_users_mutex);
    totalNumberOfUsers--;
    pthread_mutex_unlock(&n_total_users_mutex);
  }else{
    pthread_detach(t1_info);
  }

  conversation_info->firstUserInfo = NULL;
  conversation_info->secondUserInfo = NULL;
  conversation_info->waitlist = NULL;
  free (conversation_info);
  return 0;

  user1_disconnected:
  // Comunica al secondo utente che è finita la conversazione
  sprintf(send_buff, "\n%s has closed the conversation\nLooking for someone else ...\nCtrl+C to exit ...\n",conversation_info->firstUserInfo->nickname);
  write(secondUserSD,send_buff,strlen(send_buff));

  // LOGGING DISCONNECTIONS
  printf("\n-A CLIENT DISCONNECTED :\nNickname : %s\nSocket Descriptor : %d\nIP ADDRESS : %s\n",conversation_info->firstUserInfo->nickname,firstUserSD,conversation_info->firstUserInfo->IP_address);
  close(firstUserSD);
  free(conversation_info->firstUserInfo);

  pthread_mutex_lock(&n_total_active_chats_mutex);
  totalNumberOfActiveChats--;
  pthread_mutex_unlock(&n_total_active_chats_mutex);

  pthread_mutex_lock(&n_total_users_mutex);
  totalNumberOfUsers--;
  pthread_mutex_unlock(&n_total_users_mutex);

  // Fa ritornare il secondo utente in attesa di chattare
  linkedListNode* new_node_user1 ;
  if ( (new_node_user1=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
    new_node_user1->data = conversation_info->secondUserInfo ;
    new_node_user1->next = NULL ;
  }
  insert_element(new_node_user1,conversation_info->waitlist);
  conversation_info->firstUserInfo = NULL;
  conversation_info->secondUserInfo = NULL;
  conversation_info->waitlist = NULL;
  free (conversation_info);
  return 0;

  user2_stopped:
  // Comunica al primo utente che è finita la conversazione
  sprintf(send_buff, "\n%s has closed the conversation\nLooking for someone else ...\nCtrl+C to exit ...\n",conversation_info->secondUserInfo->nickname);
  write(firstUserSD,send_buff,strlen(send_buff));
  sprintf(send_buff, "\nYou have closed the conversation and stopped rolling...\n\n***** BENVENUTI IN RANDOMCHAT ! *****\n\n--- Digitare //command:<HELP> per conoscere i comandi disponibili ---\n\n");
  write(secondUserSD,send_buff,strlen(send_buff));

  pthread_mutex_lock(&n_total_active_chats_mutex);
  totalNumberOfActiveChats--;
  pthread_mutex_unlock(&n_total_active_chats_mutex);

  // Fa ritornare il primo utente in attesa di chattare
  linkedListNode* new_node_user2_stop ;
  if ( (new_node_user2_stop=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
    new_node_user2_stop->data = conversation_info->firstUserInfo ;
    new_node_user2_stop->next = NULL ;
  }
  insert_element(new_node_user2_stop,conversation_info->waitlist);

  // Affida la gestione del secondo utente al thread "manage_a_single_client", e nel caso ci sia un errore, effettua la disconnessione.
  pthread_t t2_info;
  int err2 ;
  if ( (err2=pthread_create(&t2_info, NULL, manage_a_single_client, (void*)conversation_info->secondUserInfo) ) ) {
    char threadErrorMessage[128] = "Error from the server, please restart the client !\n\0";
    write(firstUserSD,threadErrorMessage,128);

    // LOGGING DISCONNECTION
    printf("\n-A CLIENT DISCONNECTED :\nNickname : %s\nSocket Descriptor : %d\nIP ADDRESS : %s\n",conversation_info->firstUserInfo->nickname,firstUserSD,conversation_info->firstUserInfo->IP_address);
    close(firstUserSD);
    free(conversation_info->firstUserInfo);
    pthread_mutex_lock(&n_total_users_mutex);
    totalNumberOfUsers--;
    pthread_mutex_unlock(&n_total_users_mutex);
  }else{
    pthread_detach(t2_info);
  }

  conversation_info->firstUserInfo = NULL;
  conversation_info->secondUserInfo = NULL;
  conversation_info->waitlist = NULL;
  free (conversation_info);
  return 0;

  user2_disconnected:
  // Comunica al primo utente che è finita la conversazione
  sprintf(send_buff, "\n%s has closed the conversation\nLooking for someone else ...\nCtrl+C to exit ...\n",conversation_info->secondUserInfo->nickname);
  write(firstUserSD,send_buff,strlen(send_buff));

  pthread_mutex_lock(&n_total_active_chats_mutex);
  totalNumberOfActiveChats--;
  pthread_mutex_unlock(&n_total_active_chats_mutex);

  // LOGGING DISCONNECTIONS
  printf("\n-A CLIENT DISCONNECTED :\nNickname : %s\nSocket Descriptor : %d\nIP ADDRESS : %s\n",conversation_info->secondUserInfo->nickname,secondUserSD,conversation_info->secondUserInfo->IP_address);
  close(secondUserSD);
  free(conversation_info->secondUserInfo);

  pthread_mutex_lock(&n_total_users_mutex);
  totalNumberOfUsers--;
  pthread_mutex_unlock(&n_total_users_mutex);

  // Fa ritornare il primo utente in attesa di chattare
  linkedListNode* new_node_user2 ;
  if ( (new_node_user2=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
    new_node_user2->data = conversation_info->firstUserInfo ;
    new_node_user2->next = NULL ;
  }
  insert_element(new_node_user2,conversation_info->waitlist);
  conversation_info->firstUserInfo = NULL;
  conversation_info->secondUserInfo = NULL;
  conversation_info->waitlist = NULL;
  free (conversation_info);
  return 0;

  reroll:

  sprintf(send_buff, "\nConversation is ended ... Looking for someone else ...\nCtrl+C to exit ...\n");
  write(firstUserSD,send_buff,strlen(send_buff));
  write(secondUserSD,send_buff,strlen(send_buff));

  pthread_mutex_lock(&n_total_active_chats_mutex);
  totalNumberOfActiveChats--;
  pthread_mutex_unlock(&n_total_active_chats_mutex);

  linkedListNode* new_node_reroll ;
  if ( (new_node_reroll=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
    new_node_reroll->data = conversation_info->firstUserInfo ;
    new_node_reroll->next = NULL ;
  }
  insert_element(new_node_reroll,conversation_info->waitlist);
  if ( (new_node_reroll=(linkedListNode*)malloc(sizeof(linkedListNode))) != NULL ){
    new_node_reroll->data = conversation_info->secondUserInfo ;
    new_node_reroll->next = NULL ;
  }
  insert_element(new_node_reroll,conversation_info->waitlist);
  conversation_info->firstUserInfo = NULL;
  conversation_info->secondUserInfo = NULL;
  conversation_info->waitlist = NULL;
  free (conversation_info);
  return 0;
}
