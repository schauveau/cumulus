
#pragma once


typedef struct http_ui_t http_ui_t ;


// Must be implemened by the application to expand variables
// during html preprocessing.  
//
//  - varname is the name of the variable that need to be expanded.
//
// 
//
// Return a pointer to a string containing the expended value for the variable.
// 
// 
//
//void http_ui_var_producer(const char *varname, http_ui_var_consumer_t *consumer) ;


//void http_ui_send(http_ui_var_consumer_t *consumer, const char *data) ;

void start_http_ui( ) ;
