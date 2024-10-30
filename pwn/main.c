#include <stdio.h>
#include "hmalloc.h"

#define CMD_CREATE  0x10
#define CMD_ADD     0x11
#define CMD_DELETE  0x12
#define CMD_SELL    0x13
#define CMD_DROP    0x14

#define MAX_OBJS      1000
#define BUF_SZ        4096
#define MAX_STRING_SZ 1024

int total_earnings  = 0;
int total_stocks    = 0;
int total_objs      = 0;
struct object* objs[MAX_OBJS];

struct object{
  int   id;
  int   price;
  char* name;
  char* description;
  int   stock;
  int   earnings;
  void (*sell) (struct object* this);
  void (*add) (struct object* this);
  void (*drop) (struct object* this);
};

void function_sell(struct object* this){
  printf("CMD_SELL\n");
  if(this->stock <= 0)
    return;
  this->stock--;
  this->earnings = this->earnings + this->price;
}

void function_add(struct object* this){
  printf("CMD_ADD\n");
  this->stock++;
}
void execute_system(const char* cmd){
  system(cmd);
}

void notify_end(){
  const char* cmd = "touch /tmp/end";
  execute_system(cmd);
}

void function_drop(struct object* this){
  printf("CMD_DROP\n");
  this->stock--;
}

void show_object(struct object* obj){
    printf("\tid: 0x%x\n", obj->id);
    printf("\tPrice: %d\n", obj->price);
    printf("\tName: %s\n", obj->name);
    printf("\tDescription: %s\n", obj->description);
    printf("\tStock: %d\n", obj->stock);
    printf("\tEarnings: %d\n", obj->earnings);
}

struct object* get_object(short int obj_id){
    // Search for the product id
    int j = 0;
    while(j < total_objs){
      if((short int) objs[j]->id == obj_id)
        return objs[j];
      j++;
    }
    return NULL;
}

int main(){
  char raw_input[BUF_SZ];
  int n_read;
  size_t n;
  struct object* obj;
  int obj_id;
  memset(raw_input, 0x0, BUF_SZ);

  /* Parse file from input */
  n_read = read(0, raw_input, BUF_SZ);
  int idx = 0;
  while(idx < n_read){
    int command = raw_input[idx];
    idx++;
    switch(command){
      case CMD_CREATE:
        printf("CMD_CREATE\n");
        obj = malloc(sizeof(struct object));
        memset(obj, 0x0, sizeof(struct object));
        /* Object ID*/
        memcpy(&obj->id, raw_input + idx, 2);
        idx = idx + 2;

        /* Object Price*/
        memcpy(&obj->price, raw_input + idx, 2);
        idx = idx + 2;

        /* Object name */
        n = strlen((char*) raw_input + idx);
        if(n == 0 || n > MAX_STRING_SZ || (idx + n) > n_read)
          return -1;
        obj->name = malloc(n);
        memcpy(obj->name, raw_input + idx, n);
        idx = idx + n + 1;

        /* Object description */
        n = strlen((char*) raw_input + idx);
        if(n == 0 || n > MAX_STRING_SZ || (idx + n) > n_read)
          return -1;
        obj->description = malloc(n);
        memcpy(obj->description, raw_input + idx, n);
        idx = idx + n + 1;

        obj->sell = function_sell;
        obj->add  = function_add;
        obj->drop = function_drop;

        if(total_objs > MAX_OBJS)
          return -1;

        objs[total_objs] = obj;
        total_objs++;
        break;
    case CMD_DROP:
    case CMD_ADD:
    case CMD_SELL:
        memcpy(&obj_id, raw_input + idx, 2);
        idx = idx + 2;
        obj = get_object(obj_id);
        if(obj == NULL)
          break;

        if(command == CMD_ADD)
          obj->add(obj);
        else if(command == CMD_DROP)
          obj->drop(obj);
        else if(command == CMD_SELL)
          obj->sell(obj);
        break;
  case CMD_DELETE:
        printf("CMD_DELETE\n");
        memcpy(&obj_id, raw_input + idx, 2);
        idx = idx + 2;
        obj = get_object(obj_id);
        if(obj == NULL)
          break;

        free(obj->name);
        free(obj->description);
        free(obj);
        break;
      default:
        fprintf(stdout, "Command 0x%x not found\n", command);
        return -1;
        break;
    }
    if(raw_input[idx] != 0x0a){
      fprintf(stderr, "Missing newline");
      exit(EXIT_FAILURE);
    }
    idx++;
  }
  notify_end();
  return 0;
}

