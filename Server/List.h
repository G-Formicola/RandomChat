#ifndef LIST_H
#define LIST_H

#include<stdlib.h>
#include<pthread.h>

// Client informations
typedef struct client_inf {
    char IP_address[32]; // Holds the client IP address
    char nickname[32]; // Holds the nickname chosen by the the user
    int client_sd ; // The socket_descriptor opened between client and server
    struct client_inf* last_chat ; // Pointer to the last user we chatted with. Usefull for avoiding two random chats in a row with the same user
} thread_arg ;

// Node used by the linked list
typedef struct node {
  thread_arg* data ;
  struct node* next ;
} linkedListNode ;

// A simple thread_safe data structure which will holds the different rooms' clients that are waiting to chat with a random stranger. Can't be allocated statically, and the pointer must be initialized with createANewLinkedList() function defined below
typedef struct linked_l {
  linkedListNode* head ;
  int size ;
  pthread_mutex_t semaphore ;
} linkedList ;

// A data structure for holding information relevant to a conversation
typedef struct clients_inf {
    thread_arg* firstUserInfo; // Holds first user information
    thread_arg* secondUserInfo; // Holds second user information
    linkedList* waitlist; // Holds the waitlist in which put the clients when conversation has ended
} conversation_thread_arg ;

// LIST FUNCTIONS
// Initializes the list like a default constructor would, allocating the needed resources. To be called one time, only when we declare and allocate a linked list to avoid seg_fault
linkedList* createANewLinkedList();
// Insert on top of the list the new node. Thread safe.
void insert_element(linkedListNode* record, linkedList* list);
// Removes the record from the list if present. Thread safe.
void remove_element(linkedListNode* record, linkedList* list);
// Access to the ith element of the linkedList. If index is bigger than the dimension or smaller than zero, it returns NULL. Thread Safe.
linkedListNode* accessByIndex(int index, linkedList* list);
// Returns the size of a list. Thread safe.
int sizeOfTheList(linkedList* list);
// Like an object oriented destructor
void destroy_list(linkedList* list);

#endif
