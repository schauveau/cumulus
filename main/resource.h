#pragma once

typedef struct {
  const char * data;
  int          size; 
  const char * type;
  bool         is_str;
} resource_t ;

// Initialize the resources
// Must be called once at startup
void resource_init();

// Get the resource of the specified name or NULL
const resource_t * resource_get(const char *name);

//
// Get the resource of the specified name but only if contains
// a string (so .is_str is true)
//
const resource_t * resource_get_string(const char *name);

// Return true if a resource exists and contains a string.
bool resource_is_string(const char *name);

// Restore a resource to its initial state
void resource_restore(const char *name);

// Restore all resources to their initial state
void resource_restore_all();

// Temporarily update the data associated to a binary resource.
//
// The data must have been allocated with malloc and the caller
// should never attempt to free it. 
//
// Return true in case of success and false otherwise.
//
void resource_update_binary(const char *name, void *data, int size);

// Temporarily update the data associated to a string resource.
//
// The data must have been allocated with malloc and the caller
// should never attempt to free it. 
//
// The size shall match strlen(data) and so, does not include
// the trailing \0. 
// 
void resource_update_string(const char *name, char *data, int size);
