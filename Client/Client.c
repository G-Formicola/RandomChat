#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<stdlib.h>
#include<pthread.h>
#include<signal.h>
#include<errno.h>

#define MYPORT 23456
#define SERVERADDRESS "20.19.208.169"
#define MAXSLEEP 8 // Used by connect_retry
#define BUF_SIZE 1024

int server_socket_descriptor;
char nickname[32]; // Holds the nickname chosen by the the user


// Returns -1 if the client decides not to connect, a positive socket descriptor otherwise
int open_communication();
// Used by open_communication(), Returns the Socket Descriptor if connection gets approved, -1 if connection fails
int connect_retry(int domain, int type, int protocol, const struct sockaddr *addr, socklen_t alen);
// Entrypoint of the thread which will take care of receiveing responses
void * receiver(void * args);
// Handler of signals
void signalHandler (int numSignal);

int main(int argc, char* argv[]){

  char send_buff[BUF_SIZE];
  pthread_t receiver_thread = 0;

  memset(&nickname, '\0', sizeof(nickname));

  printf("\n---------- PROGETTO LABORATORIO DI SISTEMI OPERATIVI A.A. 21/22 ----------\n");
  printf("-                                                                        -");
  printf("\n- Sviluppato da : Formicola Giorgio N86/2220 & Antonio Natale N86/2769  -\n\n");


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

  // If opening goes well
  if((server_socket_descriptor = open_communication())>0){

    // Launching the thread responsable of receiving messages
    pthread_create(&receiver_thread,NULL,&receiver,NULL);

    // Let the user choice a nickname for chatting
    int nick_lenght = 0, choice_lenght = 0;
    char choice[32];
    memset(choice, '\0', sizeof(choice));
    do {
      printf("\nImmettere un nickname compreso tra 3 e 32 caratteri -> ");
      if(fgets(nickname, 32, stdin)==NULL)
        goto exithandler;
      nick_lenght = strlen(nickname);
      if(nick_lenght<31)
        nickname[nick_lenght-1]='\0'; // Replace the last char (often a newline) with the string endline char if the string doesn't fill the buffer
      nick_lenght = strlen(nickname);

      // Confirm the choice
      do {
        printf("\nIl nickname scelto è : \"%s\" Confermi ? yes/no -> ",nickname);
        if(fgets(choice, 32, stdin)==NULL)
          goto exithandler;
        choice_lenght = strlen(choice);
        choice[choice_lenght-1]='\0';
        if ( strcmp(choice,"no")!=0 && strcmp(choice,"yes")!=0 ){
          printf("\nAttenzione scelta non consentita, riprovare. \n ");
        }
      } while( strcmp(choice,"no")!=0 && strcmp(choice,"yes")!=0 );

    } while(nick_lenght < 3 || strcmp(choice,"no")==0 || strcmp(choice,"yes")!=0 ); // Minimum 3 char plus endline char '\0'

    sprintf(send_buff, "//command:NICKNAME<%s>\n", nickname);
    if (write(server_socket_descriptor,send_buff,strlen(send_buff)) < strlen(send_buff)){
      printf("\nAttenzione, errore durante l'invio del nickname al server\nSi prega di riavviare il client\n");
      goto exithandler;
    }

    printf("\n***** BENVENUTI IN RANDOMCHAT ! *****\n\n");
    printf("--- Digitare //command:<HELP> per conoscere i comandi disponibili ---\n\n");
    printf("\\/\n");
    // If user prompts Ctrl-D (EOF) or the server stops to respond the client will exit;
	  while (fgets(send_buff, BUF_SIZE-1, stdin)!=NULL){
         send_buff[strlen(send_buff)]='\0';
         int n_written_char;
         int message_lenght = strlen(send_buff);
         if ( (n_written_char = write(server_socket_descriptor,send_buff,message_lenght )) == message_lenght )
		       printf("-sent\n\n");
         else
           printf("-error sending the message : \n-only %d characters sent, try again. \n\n",n_written_char);
	   }
  }

  exithandler:
  printf("Closing the main thread of the client ...\n\n");
  close(server_socket_descriptor);
  if(receiver_thread!=0){
    if ((pthread_join(receiver_thread,NULL))!=0)
      printf("\nError pthread_join : %s\n",strerror(errno));
  }
  printf("*** GOODBYE %s ! ***\n",nickname );
  return 0;
}

// Returns -1 if the client decides not to connect, a positive socket descriptor otherwise
int open_communication(){

  int socket_descriptor ;

  struct sockaddr_in server_address ;

  memset(&server_address, '\0', sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(MYPORT);
  inet_aton(SERVERADDRESS, &server_address.sin_addr);

  socklen_t server_addr_len = sizeof(server_address);

  int user_choice;

  char inputString[128];

  do {

    printf("\nConnessione al server in corso ... attendere prego ... \n");
    socket_descriptor = connect_retry(PF_INET, SOCK_STREAM, 0, (struct sockaddr*)&server_address, server_addr_len);

    if (socket_descriptor < 0){

      printf("\nConnessione al server non riuscita, riprovare ? \n");
      printf("1 = Riprova a connetterti \n");
      printf("0 = Esci \n");
      printf("----- \n");

      // Asks for a choice until the client doesn't insert an available one
      while(1) {
        printf("Scelta : ");

        fgets(inputString, 128, stdin);
        inputString[strlen(inputString)-1]='\0'; //remove '\n'

        char* c = NULL;

        user_choice = (int) strtol(inputString, &c, 10);

        if ( c == NULL || *c != '\0' || user_choice<0 || user_choice>1)
        {
          printf("Attenzione : scelta non consentita, riprovare : %s\n",inputString);
        }
        else
        {
          printf("\n");
          break;
        }
      }

    }else{
      printf("Connessione avvenuta con successo !\n\n");
    }

  } while( socket_descriptor < 0 && user_choice!=0 );

  return socket_descriptor;
}

// Returns the Socket Descriptor if connection gets approved, -1 if connection fails. Used by open_communication()
int connect_retry(int domain, int type, int protocol, const struct sockaddr *addr, socklen_t alen){
    int numsec, fd;
    /*
     * Try to connect with exponential backoff.
     */
    for (numsec = 1; numsec <= MAXSLEEP; numsec <<= 1) {
        printf("... \n");
        if ((fd = socket(domain, type, protocol)) < 0)
            return(-1);
        if (connect(fd, addr, alen) == 0) {
            /*
             * Connection accepted.
             */
            return(fd);
        }
        close(fd);
        /*
         * Delay before trying again.
         */
        if (numsec <= MAXSLEEP/2)
            sleep(numsec);
    }

    return(-1);

}

// Entrypoint of the thread which will take care of receiveing responses, and printing them on the standard output
void * receiver(void * args){

	char recv_buff[BUF_SIZE];
	int n_read_char, dim_recv_messagge = 0;

  while ( (n_read_char = read(server_socket_descriptor, recv_buff+dim_recv_messagge, BUF_SIZE-dim_recv_messagge-2)) != 0){
     if(n_read_char < 0){
       goto exit_thread;
   	 }else{
       dim_recv_messagge += n_read_char;
       // If the space in the buffer has finished, or newline appears in the string then we can print the response from the server
       if(dim_recv_messagge >= BUF_SIZE-2 || recv_buff[dim_recv_messagge-1] == '\n'){

         if(dim_recv_messagge < BUF_SIZE-2)
           recv_buff[dim_recv_messagge]='\0';
         else // buffer is full
           recv_buff[BUF_SIZE-1]='\0';

         printf("\n-received :\n\\/ %s \n\n\\/\n ",recv_buff);
       }
     }
      dim_recv_messagge = 0;

  }

   if (n_read_char==0){
      printf("\nConnection closed by the Server\n\n");
   }

  exit_thread:
  printf("Closing the client thread for receiving ...\n\n");
  close(server_socket_descriptor);
  // If the connection has been closed by the server
  if (n_read_char==0){
    printf("*** GOODBYE %s ! ***\n\n",nickname );
    exit(0);
  }else{ // If the connection has been closed by the client
    pthread_exit(0);
  }
}

// Handler of signals
void signalHandler (int numSignal){
  if (numSignal == SIGINT){
    printf("\nClosing the client ...\n\n");
    close(server_socket_descriptor);
    printf("*** GOODBYE %s ! ***\n\n",nickname );
    exit(0);
  }
  if (numSignal == SIGPIPE){
    printf("Error trying to send data...\nPlease try again and if error persists, restart the client ...\n\n");
  }
}
