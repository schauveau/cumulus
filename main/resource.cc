
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>

#include "esp_log.h"

#include "resource.h"

static const char TAG[] = "resource" ;

//
// According to the official ESP-IDF documentation, each embedded file is encapsulated by
// two symbols _binary_SYMBOL_start and _binary_SYMBOL_end
//
//
//

#define START(SYMBOL)  _binary_##SYMBOL##_start
#define END(SYMBOL)    _binary_##SYMBOL##_end

typedef struct {
  const char * name;
  const char * start;
  const char * end; 
} priv_resource_t ;

#include "resource.inc"

// Declare all the symbols

#define DEF_TEXT(NAME, SYMBOL, TYPE)   extern const char START(SYMBOL)[] , END(SYMBOL) [] ;
#define DEF_BINARY(NAME, SYMBOL, TYPE) extern const char START(SYMBOL)[] , END(SYMBOL) [] ;

RESOURCES

#undef DEF_TEXT
#undef DEF_BINARY

#define DEF_TEXT(NAME, SYMBOL, TYPE)   { .name = NAME, .start = START(SYMBOL), .end = END(SYMBOL) },
#define DEF_BINARY(NAME, SYMBOL, TYPE) { .name = NAME, .start = START(SYMBOL), .end = END(SYMBOL) },

static const priv_resource_t priv[] = {
  RESOURCES
};

#undef DEF_TEXT
#undef DEF_BINARY

#define DEF_TEXT(NAME, SYMBOL, TYPE)   { .data=0, .size=0, .type=TYPE, .is_str=true, }, 
#define DEF_BINARY(NAME, SYMBOL, TYPE) { .data=0, .size=0, .type=TYPE, .is_str=false,}, 

static resource_t resources[] = {
  RESOURCES
};
  
#undef DEF_TEXT
#undef DEF_BINARY

//////
//////
//////

enum {
  // The number of resources. 
  rescount = sizeof(resources)/sizeof(resource_t)
} ;

static int resource_index(const char *name) {
  if (!name)
    return -1 ;
  for (int i=0;i<rescount;i++) {
    if (!strcmp(priv[i].name, name)) {
      return i;
    }
  }
  return -1;
}

const resource_t *
resource_get(const char *name)
{
  int i = resource_index(name) ;
  if (i>=0) {
    return &resources[i] ;
  }
  return NULL; 
}

const resource_t *
resource_get_string(const char *name)
{
  const resource_t * res = resource_get(name) ;
  if (res && res->is_str) {
    return res;
  }
  return NULL ;
}

bool
resource_is_string(const char *name)
{
  const resource_t * res = resource_get(name) ;
  return (res && res->is_str) ;
}


static void
resource_restore_at(int i)
{
  if (i>=0 && i<rescount) {
    if ( resources[i].data && resources[i].data != priv[i].start ) {      
      free( (void*) resources[i].data ) ;
    }
    resources[i].data = priv[i].start ;
    int size = priv[i].end - priv[i].start;
    if (resources[i].is_str)
      size--;
    resources[i].size = size ;
  }
}

void
resource_restore(const char *name)
{
  int i = resource_index(name) ;
  resource_restore_at(i);  
}

void
resource_restore_all()
{
  for (int i=0;i<rescount; i++) {   
    resource_restore_at(i);
  }
}

void
resource_update_binary(const char *name, void *data, int size)
{
  if (data==NULL || size==0) {
    return;
  }
  
  int i = resource_index(name);
  if (i<0) {
    free(data);
    return;
  }

  if (resources[i].is_str) {
    ESP_LOGE(TAG,"Cannot update string resource '%s' with binary data",name);
    free(data);
    return; 
  }

  resource_restore_at(i); /// free the current resource if necessary 

  resources[i].data = (const char*) data ; 
  resources[i].size = size ; 
}

void
resource_update_string(const char *name, char *data, int size)
{
  if (data==NULL || size==0) {
    return;
  }
  
  int i = resource_index(name);
  if (i<0) {
    free(data);
    return;
  }

  if (!resources[i].is_str) {
    ESP_LOGE(TAG,"Cannot update binary resource '%s' with string data",name);
    free(data);
    return ;
  }

  // Make sure that data is a valid string of the specified size
  if ( data[size]!=0 || strlen(data)!=size ) {
    ESP_LOGE(TAG,"Malformed data for string resource '%s'",name);
    free(data);
    return ;
  }
  
  resource_restore_at(i); // free the current resource if necessary

  resources[i].data = data ; 
  resources[i].size = size ; 
}

void resource_init()
{  
  resource_restore_all() ;
  
  for (int i=0;i<rescount; i++) {
    ESP_LOGI(TAG, "%c %5d %s ", (resources[i].is_str?'S':'B'),  resources[i].size, priv[i].name );
  }
}

