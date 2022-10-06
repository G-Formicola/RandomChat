#include "List.h"

// LIST FUNCTIONS
// Initializes the list like a default constructor would, allocating the needed resources. To be called one time, only when we declare and allocate a linked list to avoid seg_fault
linkedList* createANewLinkedList(){
    linkedList* ret_list ;
    if ( (ret_list=(linkedList*)malloc(sizeof(linkedList)))!=NULL ){
      ret_list->head = NULL ;
      ret_list->size = 0 ;
      pthread_mutex_init(&ret_list->semaphore,NULL);
    }
    return ret_list;
}
// Insert on top of the list the new node. Thread safe.
void insert_element(linkedListNode* record, linkedList* list){
  if (record!=NULL && list!=NULL)  {
    pthread_mutex_lock(&list->semaphore);
    record->next = list->head;
    list->head = record ;
    list->size++ ;
    pthread_mutex_unlock(&list->semaphore);
  }
}
// Removes the record (found with accessByIndex function) from the list if still present. Thread safe.
void remove_element(linkedListNode* record, linkedList* list){
  if (record!=NULL && list!=NULL)  {
      pthread_mutex_lock(&list->semaphore);
      linkedListNode* iterator = list->head ;
      linkedListNode* last_visited = NULL ;
      while (iterator!=NULL && iterator!=record){
        last_visited = iterator ;
        iterator = iterator->next ;
      }
      if (iterator!=NULL){
        if(last_visited!=NULL){
          //rimuovi dal centro
          last_visited->next = iterator->next ;
        }else{
          //rimuovi dalla testa
          list->head = iterator->next ;
        }
        iterator->data = NULL ;
        iterator->next = NULL ;
        free(iterator);
        list->size-- ;
      }
      pthread_mutex_unlock(&list->semaphore);
  }
}
// Access to the ith element of the linkedList. If index is bigger than the dimension or smaller than zero, it returns NULL. Thread Safe.
linkedListNode* accessByIndex(int index, linkedList* list){
  linkedListNode* ret_value = NULL ;
  if (list!=NULL){
    pthread_mutex_lock(&list->semaphore);
    linkedListNode* iterator = list->head ;
    if (index < list->size && index>=0){
      for (int i = 0; i < index; i++) {
        iterator = iterator->next ;
      }
      ret_value = iterator ;
    }
    pthread_mutex_unlock(&list->semaphore);
  }
  return ret_value;
}
// Returns the size of a list. Thread safe.
int sizeOfTheList(linkedList* list){
  int ret_value=-1;
  if (list!=NULL){
    pthread_mutex_lock(&list->semaphore);
    ret_value = list->size ;
    pthread_mutex_unlock(&list->semaphore);
  }
  return ret_value;
}

void destroy_list(linkedList* list){
  if (list!=NULL){
    linkedListNode* iterator = list->head ;
    while (iterator!=NULL){
      list->head = list->head->next ;
      iterator->next = NULL;
      free(iterator->data);
      iterator->data = NULL;
      free(iterator);
      iterator = list->head ;
    }
    list->head = NULL;
    pthread_mutex_destroy(&list->semaphore);
    free(list) ;
  }
}
